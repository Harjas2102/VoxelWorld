// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "Misc/AutomationTest.h"
#include "TerrainStorage.h"
#include "TerrainPersistenceRecords.h"
#include "TerrainPersistenceFixtures.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The storage device seam and the two stores on it (P-004 §11, §12).
 *
 * Most of this runs against FTerrainMemoryStorageDevice and is headless. One case runs
 * against the real file system, because the two properties that matter most -- that an
 * in-place overwrite does not truncate, and that an append lands where it should -- are
 * properties of IPlatformFile and cannot be tested against a fake.
 */

namespace TerrainStorageTest
{
	TArray<uint8> Pattern(int32 Length, uint8 Seed)
	{
		TArray<uint8> Out;
		Out.Reserve(Length);
		for (int32 Index = 0; Index < Length; ++Index)
		{
			Out.Add(static_cast<uint8>((Seed + Index * 7) & 0xFF));
		}
		return Out;
	}

	/** A valid 4000-byte root-slot body carrying a chosen generation. */
	TArray<uint8> RootBody(uint64 Generation)
	{
		FTerrainRootSlot Root;
		Root.Generation         = Generation;
		Root.G                  = Generation * 10;
		Root.DescriptorDigest   = TerrainPersistTest::MakeDigest(static_cast<uint8>(Generation));
		Root.DescriptorLength   = 176;
		Root.PublishedUtcMillis = 1789412345678LL;

		TArray<uint8> Body;
		verify(TerrainPersistEncodeRootSlotBody(Root, Body) == ETerrainPersistError::None);
		return Body;
	}
}

// ==== Paths =============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainStoragePathsTest,
	"TerrainCore.Persistence.Storage.Paths",
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

bool FTerrainStoragePathsTest::RunTest(const FString& Parameters)
{
	using namespace TerrainPersistTest;

	// P-004 §11.1: an object's name is derived from its digest by a fixed rule and nothing else.
	{
		const FTerrainDigest Digest = MakeDigest(0xAB);
		const FString Path = TerrainStoragePaths::Object(Digest);
		const FString Hex  = TerrainPersistDigestToHex(Digest);

		AddInfo(FString::Printf(TEXT("Object path: %s"), *Path));
		TestEqual(TEXT("The object path is objects/<hh>/<64hex>.tobj"),
			Path, FString::Printf(TEXT("objects/ab/%s.tobj"), *Hex));
		TestTrue(TEXT("and it is a safe relative path"), TerrainStorageIsSafeRelativePath(Path));

		TestEqual(TEXT("Root slot 0"), TerrainStoragePaths::RootSlot(0), FString(TEXT("roots/root.0")));
		TestEqual(TEXT("Root slot 1"), TerrainStoragePaths::RootSlot(1), FString(TEXT("roots/root.1")));
		TestEqual(TEXT("Anchor slot 0"), TerrainStoragePaths::AnchorSlot(0), FString(TEXT("journal/anchor.0")));
		TestEqual(TEXT("Segment 9"), TerrainStoragePaths::JournalSegment(9),
			FString(TEXT("journal/seg-0000000000000009.tjs")));

		TestTrue(TEXT("Every generated path is safe"),
			TerrainStorageIsSafeRelativePath(TerrainStoragePaths::RootSlot(1))
			&& TerrainStorageIsSafeRelativePath(TerrainStoragePaths::AnchorSlot(0))
			&& TerrainStorageIsSafeRelativePath(TerrainStoragePaths::JournalSegment(0xFFFFFFFFFFFFFFFFull))
			&& TerrainStorageIsSafeRelativePath(TerrainStoragePaths::BaseDescriptor));
	}

	// The backstop. Nothing legitimate needs any of these, and every one of them is a way out
	// of the world directory on some platform.
	{
		const TCHAR* Unsafe[] = {
			TEXT(""),
			TEXT("/etc/passwd"),
			TEXT("C:/Windows/System32/config"),
			TEXT("..\\..\\secrets"),
			TEXT("objects/../../../etc/passwd"),
			TEXT("objects/./ab/x.tobj"),
			TEXT("objects//ab/x.tobj"),
			TEXT("objects/ab/"),
			TEXT("objects\\ab\\x.tobj"),
			TEXT("objects/ab/x y.tobj"),
			TEXT("objects/ab/x*.tobj"),
			TEXT(".."),
		};
		for (const TCHAR* Path : Unsafe)
		{
			TestFalse(FString::Printf(TEXT("'%s' is refused"), Path),
				TerrainStorageIsSafeRelativePath(FString(Path)));
		}

		TestTrue(TEXT("An ordinary generated name is accepted"),
			TerrainStorageIsSafeRelativePath(TEXT("journal/seg-0000000000000001.tjs")));
	}

	return true;
}

// ==== ObjectStore =======================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainStorageObjectStoreTest,
	"TerrainCore.Persistence.Storage.ObjectStore",
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

bool FTerrainStorageObjectStoreTest::RunTest(const FString& Parameters)
{
	using namespace TerrainPersistTest;
	using namespace TerrainStorageTest;

	FTerrainMemoryStorageDevice Device;
	FTerrainFileObjectStore Store(Device);
	TestEqual(TEXT("The objects directory is created"),
		Store.EnsureLayout(), ETerrainStorageResult::Ok);

	const TArray<uint8> Object = Pattern(512, 0x11);
	const FTerrainDigest Digest = TerrainPersistDigest(Object);

	TestFalse(TEXT("An absent object is absent"), Store.Contains(Digest));
	{
		TArray<uint8> Out;
		TestFalse(TEXT("and loading it fails"), Store.LoadObject(Digest, Out));
	}

	TestTrue(TEXT("Storing succeeds"), Store.StoreObject(Digest, Object));
	TestTrue(TEXT("and the object is now present"), Store.Contains(Digest));
	TestTrue(TEXT("at exactly the path the naming rule gives"),
		Device.Exists(TerrainStoragePaths::Object(Digest)));

	{
		TArray<uint8> Out;
		TestTrue (TEXT("Loading succeeds"), Store.LoadObject(Digest, Out));
		TestTrue (TEXT("and returns the same bytes"), Out == Object);
	}

	// Content addressing is checked on the way IN.
	{
		const FTerrainDigest Wrong = MakeDigest(0x01);
		TestFalse(TEXT("Storing under a digest the bytes do not hash to is refused"),
			Store.StoreObject(Wrong, Object));
		TestFalse(TEXT("and nothing was written"), Device.Exists(TerrainStoragePaths::Object(Wrong)));
	}

	// Storing the same object twice is a success that writes nothing.
	{
		const int32 Before = Device.NumFiles();
		TestTrue (TEXT("Storing the same object again succeeds"), Store.StoreObject(Digest, Object));
		TestEqual(TEXT("and adds no file"), Device.NumFiles(), Before);
	}

	// ...and on the way OUT. This is the property that lets a reader trust the bytes without
	// trusting the medium: bit rot under the store is detected, not returned.
	{
		TArray<uint8>* OnDisk = Device.Find(TerrainStoragePaths::Object(Digest));
		TestNotNull(TEXT("The object file is reachable for damage"), OnDisk);
		(*OnDisk)[100] ^= 0xFF;

		TArray<uint8> Out;
		TestFalse(TEXT("A damaged object fails to load rather than returning bad bytes"),
			Store.LoadObject(Digest, Out));

		(*OnDisk)[100] ^= 0xFF;
		TestTrue(TEXT("and loads again once repaired"), Store.LoadObject(Digest, Out));
	}

	// A write fault leaves no object behind, and the store reports failure.
	{
		FTerrainFaultDevice Faulty(Device);
		FTerrainFileObjectStore FaultyStore(Faulty);

		const TArray<uint8> Second = Pattern(300, 0x77);
		const FTerrainDigest SecondDigest = TerrainPersistDigest(Second);

		Faulty.FailAfter(ETerrainStorageOp::WriteNew, 0);
		TestFalse(TEXT("A failed write is reported as failure"),
			FaultyStore.StoreObject(SecondDigest, Second));
		TestFalse(TEXT("and leaves no object"), FaultyStore.Contains(SecondDigest));

		Faulty.ClearFaults();
		TestTrue(TEXT("and the retry succeeds"), FaultyStore.StoreObject(SecondDigest, Second));
	}

	// A TORN write leaves a file whose content does not hash to its name. It must not load.
	{
		FTerrainFaultDevice Faulty(Device);
		FTerrainFileObjectStore FaultyStore(Faulty);

		const TArray<uint8> Third = Pattern(400, 0x33);
		const FTerrainDigest ThirdDigest = TerrainPersistDigest(Third);

		Faulty.TearAfter(ETerrainStorageOp::WriteNew, 0, /*TearBytes=*/128);
		TestFalse(TEXT("A torn write is reported as failure"),
			FaultyStore.StoreObject(ThirdDigest, Third));
		TestTrue (TEXT("but the truncated file is on disk, as after a crash"),
			Device.Exists(TerrainStoragePaths::Object(ThirdDigest)));

		TArray<uint8> Out;
		TestFalse(TEXT("and it does NOT load, because it does not hash to its own name"),
			FaultyStore.LoadObject(ThirdDigest, Out));

		// Recovery from that state is a delete and a retry, and both work.
		TestEqual(TEXT("The torn object can be removed"),
			FaultyStore.DeleteObject(ThirdDigest), ETerrainStorageResult::Ok);
		Faulty.ClearFaults();
		TestTrue(TEXT("and rewritten"), FaultyStore.StoreObject(ThirdDigest, Third));
		TestTrue(TEXT("and then it loads"), FaultyStore.LoadObject(ThirdDigest, Out));
	}

	AddInfo(FString::Printf(TEXT("Store held %d files at the end of the test"), Device.NumFiles()));
	return true;
}

// ==== SlotPair ==========================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainStorageSlotPairTest,
	"TerrainCore.Persistence.Storage.SlotPair",
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

bool FTerrainStorageSlotPairTest::RunTest(const FString& Parameters)
{
	using namespace TerrainPersistTest;
	using namespace TerrainStorageTest;

	const FTerrainPersistIdentity Identity = MakeIdentity();

	FTerrainMemoryStorageDevice Device;
	Device.EnsureDirectory(TerrainStoragePaths::RootsDirectory);

	FTerrainSlotPair Slots(Device, ETerrainPersistObjectType::RootSlot,
		TerrainStoragePaths::RootSlot(0), TerrainStoragePaths::RootSlot(1));

	// --- creation ------------------------------------------------------------------------
	TestEqual(TEXT("Both slots are created"),
		Slots.Create(Identity, RootBody(1)), ETerrainStorageResult::Ok);
	TestEqual(TEXT("Slot 0 is exactly 4096 bytes"),
		Device.Size(TerrainStoragePaths::RootSlot(0)), (int64)TerrainPersistSlotSize);
	TestEqual(TEXT("Slot 1 is exactly 4096 bytes"),
		Device.Size(TerrainStoragePaths::RootSlot(1)), (int64)TerrainPersistSlotSize);
	TestEqual(TEXT("Creating over an existing pair is refused"),
		Slots.Create(Identity, RootBody(1)), ETerrainStorageResult::AlreadyExists);

	// --- publishing alternates, and never touches the current slot -------------------------
	FTerrainSlotState Zero, One;
	int32 Best = INDEX_NONE;
	TestEqual(TEXT("Both slots read"), Slots.Read(Identity, Zero, One, Best), ETerrainStorageResult::Ok);
	TestTrue (TEXT("Both are valid"), Zero.bValid && One.bValid);
	TestEqual(TEXT("Slot 0 wins the tie"), Best, 0);
	TestEqual(TEXT("so the next publish goes to slot 1"), Slots.GetNextPublishIndex(), 1);

	TestEqual(TEXT("Publishing generation 2"),
		Slots.Publish(Identity, RootBody(2)), ETerrainStorageResult::Ok);
	TestEqual(TEXT("and the next publish alternates back to slot 0"), Slots.GetNextPublishIndex(), 0);

	TestEqual(TEXT("Re-reading"), Slots.Read(Identity, Zero, One, Best), ETerrainStorageResult::Ok);
	TestEqual(TEXT("Slot 1 now holds the newer generation"), Best, 1);
	TestEqual(TEXT("which is 2"), One.Generation, (uint64)2);
	TestEqual(TEXT("and slot 0 still holds 1"), Zero.Generation, (uint64)1);

	TestEqual(TEXT("Publishing generation 3 goes to slot 0"),
		Slots.Publish(Identity, RootBody(3)), ETerrainStorageResult::Ok);
	Slots.Read(Identity, Zero, One, Best);
	TestEqual(TEXT("Slot 0 is current again"), Best, 0);
	TestEqual(TEXT("at generation 3"), Zero.Generation, (uint64)3);
	TestEqual(TEXT("and slot 1 still holds the previous generation, intact"), One.Generation, (uint64)2);

	// --- a torn publication: the OTHER slot survives ----------------------------------------
	{
		FTerrainFaultDevice Faulty(Device);
		FTerrainSlotPair Tearing(Faulty, ETerrainPersistObjectType::RootSlot,
			TerrainStoragePaths::RootSlot(0), TerrainStoragePaths::RootSlot(1));

		FTerrainSlotState A, B;
		int32 BestIndex = INDEX_NONE;
		Tearing.Read(Identity, A, B, BestIndex);
		TestEqual(TEXT("Slot 0 is current before the tear"), BestIndex, 0);
		TestEqual(TEXT("so the tear will land on slot 1"), Tearing.GetNextPublishIndex(), 1);

		// 104 = the 96-byte header plus the body's 8-byte Generation. The header and the
		// generation are NEW; everything after them is OLD. That is the nastiest torn slot
		// there is -- one whose generation field claims to be current while its content is
		// not -- and it is the case a reader that trusted the generation without validating
		// the checksum would get wrong.
		//
		// The first version of this fixture tore at byte 1000, which proved nothing: a root
		// slot body is 64 bytes of fields and 3,936 zeroes, so both images are identical past
		// byte 160 and the "tear" reproduced a perfectly valid new slot.
		Faulty.TearAfter(ETerrainStorageOp::OverwriteInPlace, 0, /*TearBytes=*/104);
		TestEqual(TEXT("The torn publish reports failure"),
			Tearing.Publish(Identity, RootBody(4)), ETerrainStorageResult::IoError);

		TestEqual(TEXT("The torn slot is still 4096 bytes -- in-place, never truncated"),
			Device.Size(TerrainStoragePaths::RootSlot(1)), (int64)TerrainPersistSlotSize);

		Faulty.ClearFaults();
		Tearing.Read(Identity, A, B, BestIndex);
		TestTrue (TEXT("The torn slot fails validation"), !B.bValid);
		TestTrue (TEXT("and it was present, so this is damage and not absence"), B.bPresent);
		AddInfo(FString::Printf(TEXT("Torn slot rejected with %s"), TerrainPersistErrorName(B.Error)));
		TestTrue (TEXT("The untouched slot is still valid"), A.bValid);
		TestEqual(TEXT("and is selected"), BestIndex, 0);
		TestEqual(TEXT("still at generation 3 -- no acknowledged state was lost"), A.Generation, (uint64)3);

		// And the retry repairs the damaged slot rather than touching the good one.
		TestEqual(TEXT("The next publish targets the damaged slot"), Tearing.GetNextPublishIndex(), 1);
		TestEqual(TEXT("and repairs it"), Tearing.Publish(Identity, RootBody(4)), ETerrainStorageResult::Ok);
		Tearing.Read(Identity, A, B, BestIndex);
		TestTrue (TEXT("Both slots are valid again"), A.bValid && B.bValid);
		TestEqual(TEXT("and generation 4 is current"), BestIndex, 1);
	}

	// --- a slot from another world is not this world's slot ---------------------------------
	{
		FTerrainPersistIdentity Other = Identity;
		Other.World.Bytes[0] ^= 0xFF;

		FTerrainSlotState A, B;
		int32 BestIndex = 0;
		Slots.Read(Other, A, B, BestIndex);
		TestFalse(TEXT("Neither slot validates against another world"), A.bValid || B.bValid);
		TestEqual(TEXT("and the failure names the reason"), A.Error, ETerrainPersistError::WorldMismatch);
		TestEqual(TEXT("There is no state to trust"), BestIndex, INDEX_NONE);
	}

	// --- publishing before reading is refused ------------------------------------------------
	{
		FTerrainSlotPair Fresh(Device, ETerrainPersistObjectType::RootSlot,
			TerrainStoragePaths::RootSlot(0), TerrainStoragePaths::RootSlot(1));
		TestEqual(TEXT("Publishing without a read is refused rather than guessing"),
			Fresh.Publish(Identity, RootBody(9)), ETerrainStorageResult::NotFound);
	}

	// --- both slots damaged: reported, not papered over ---------------------------------------
	{
		FTerrainMemoryStorageDevice Broken;
		Broken.EnsureDirectory(TerrainStoragePaths::RootsDirectory);
		FTerrainSlotPair Pair(Broken, ETerrainPersistObjectType::RootSlot,
			TerrainStoragePaths::RootSlot(0), TerrainStoragePaths::RootSlot(1));
		Pair.Create(Identity, RootBody(1));

		for (int32 Index = 0; Index < 2; ++Index)
		{
			TArray<uint8>* Bytes = Broken.Find(TerrainStoragePaths::RootSlot(Index));
			(*Bytes)[2000] ^= 0xFF;
		}

		FTerrainSlotState A, B;
		int32 BestIndex = 0;
		Pair.Read(Identity, A, B, BestIndex);
		TestFalse(TEXT("Neither damaged slot validates"), A.bValid || B.bValid);
		TestEqual(TEXT("and there is no best slot"), BestIndex, INDEX_NONE);
		TestTrue (TEXT("but both are reported present, so a caller can tell damage from a fresh world"),
			A.bPresent && B.bPresent);
	}

	return true;
}

// ==== PlatformDevice ====================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainStoragePlatformDeviceTest,
	"TerrainCore.Persistence.Storage.PlatformDevice",
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

bool FTerrainStoragePlatformDeviceTest::RunTest(const FString& Parameters)
{
	using namespace TerrainStorageTest;

	// The two properties this case exists for -- that an in-place overwrite does not truncate,
	// and that an append lands at the end -- are properties of IPlatformFile. A fake device
	// would agree with itself and prove nothing.
	const FString Root = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("Automation") / TEXT("TerrainStorageTest"));

	IFileManager::Get().DeleteDirectory(*Root, /*RequireExists=*/false, /*Tree=*/true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*Root, /*RequireExists=*/false, /*Tree=*/true);
	};

	FTerrainPlatformStorageDevice Device(Root);
	AddInfo(FString::Printf(TEXT("Platform device rooted at %s"), *Device.GetRoot()));

	TestEqual(TEXT("A nested directory is created"),
		Device.EnsureDirectory(TEXT("objects/ab")), ETerrainStorageResult::Ok);
	TestEqual(TEXT("Creating it again is Ok"),
		Device.EnsureDirectory(TEXT("objects/ab")), ETerrainStorageResult::Ok);

	const FString Path = TEXT("objects/ab/sample.tobj");
	const TArray<uint8> First = Pattern(1000, 0x20);

	TestEqual(TEXT("WriteNew succeeds"), Device.WriteNew(Path, First), ETerrainStorageResult::Ok);
	TestTrue (TEXT("and the file exists"), Device.Exists(Path));
	TestEqual(TEXT("with the right size"), Device.Size(Path), (int64)First.Num());
	TestEqual(TEXT("WriteNew over an existing file is refused"),
		Device.WriteNew(Path, First), ETerrainStorageResult::AlreadyExists);

	{
		TArray<uint8> Back;
		TestEqual(TEXT("Read succeeds"), Device.Read(Path, Back), ETerrainStorageResult::Ok);
		TestTrue (TEXT("and returns the bytes written"), Back == First);
	}

	// The truncation question, asked directly.
	{
		const TArray<uint8> Replacement = Pattern(1000, 0x50);
		TestEqual(TEXT("OverwriteInPlace with the same length succeeds"),
			Device.OverwriteInPlace(Path, Replacement), ETerrainStorageResult::Ok);
		TestEqual(TEXT("and the file is still exactly as long"), Device.Size(Path), (int64)1000);

		TArray<uint8> Back;
		Device.Read(Path, Back);
		TestTrue(TEXT("and holds the new bytes"), Back == Replacement);

		TestEqual(TEXT("A shorter overwrite is refused as WrongSize"),
			Device.OverwriteInPlace(Path, Pattern(999, 0x60)), ETerrainStorageResult::WrongSize);
		TestEqual(TEXT("and the file is untouched -- no truncation happened"),
			Device.Size(Path), (int64)1000);

		Device.Read(Path, Back);
		TestTrue(TEXT("with its content intact"), Back == Replacement);
	}

	// Append lands at the end.
	{
		const TArray<uint8> Tail = Pattern(37, 0x90);
		TestEqual(TEXT("Append succeeds"), Device.Append(Path, Tail), ETerrainStorageResult::Ok);
		TestEqual(TEXT("and the file grew by exactly that much"), Device.Size(Path), (int64)1037);

		TArray<uint8> Back;
		Device.Read(Path, Back);
		TestEqual(TEXT("The appended bytes are at the end"),
			FMemory::Memcmp(Back.GetData() + 1000, Tail.GetData(), Tail.Num()), 0);
	}

	// Path safety holds against the real file system too.
	{
		TestEqual(TEXT("An escaping path is refused before it reaches the disk"),
			Device.WriteNew(TEXT("../escaped.tobj"), First), ETerrainStorageResult::BadPath);
		TestEqual(TEXT("An absolute path is refused"),
			Device.WriteNew(TEXT("C:/escaped.tobj"), First), ETerrainStorageResult::BadPath);
		TestFalse(TEXT("and nothing was created outside the root"),
			IFileManager::Get().FileExists(*(Root / TEXT("../escaped.tobj"))));
	}

	TArray<uint8> Absent;
	TestEqual(TEXT("Missing files read as NotFound"),
		Device.Read(TEXT("objects/ab/absent.tobj"), Absent), ETerrainStorageResult::NotFound);
	TestEqual(TEXT("Deleting works"), Device.Delete(Path), ETerrainStorageResult::Ok);
	TestFalse(TEXT("and the file is gone"), Device.Exists(Path));
	TestEqual(TEXT("Deleting again is NotFound"), Device.Delete(Path), ETerrainStorageResult::NotFound);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

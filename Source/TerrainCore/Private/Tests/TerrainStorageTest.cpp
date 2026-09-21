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

	/** Where Needle first occurs in Haystack, or -1. */
	int32 FindBytes(const TArray<uint8>& Haystack, const TArray<uint8>& Needle)
	{
		for (int32 At = 0; At + Needle.Num() <= Haystack.Num(); ++At)
		{
			if (FMemory::Memcmp(Haystack.GetData() + At, Needle.GetData(), Needle.Num()) == 0)
			{
				return At;
			}
		}
		return -1;
	}

	/**
	 * A pre-P-005 pack file's bytes. A container frame's body IS a pack image (P-005 §4.1), so
	 * the writer that exists is the writer used: one frame, header stripped.
	 */
	TArray<uint8> MakeLegacyPackImage(const TArray<TArray<uint8>>& Objects)
	{
		FTerrainMemoryStorageDevice Scratch;
		FTerrainFileObjectStore Store(Scratch);
		verify(Store.EnsureLayout() == ETerrainStorageResult::Ok);
		Store.BeginBatch();
		for (const TArray<uint8>& Object : Objects)
		{
			verify(Store.StoreObject(TerrainPersistDigest(Object), Object));
		}
		verify(Store.CommitBatch() == ETerrainStorageResult::Ok);
		TArray<uint8> Frame = *Scratch.Find(TerrainStoragePaths::Container(0));
		Frame.RemoveAt(0, TerrainFrameHeaderSize);
		return Frame;
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
	bool bCreated = false;
	TestEqual(TEXT("The container pool is created"),
		Store.EnsureLayout(&bCreated), ETerrainStorageResult::Ok);
	TestTrue(TEXT("and says it created something"), bCreated);
	TestEqual(TEXT("as exactly four files"), Device.NumFiles(), TerrainStoragePaths::ContainerCount);
	TestEqual(TEXT("A second EnsureLayout is Ok"), Store.EnsureLayout(&bCreated), ETerrainStorageResult::Ok);
	TestFalse(TEXT("and creates nothing"), bCreated);
	Store.LoadPacks();

	const FString C0 = TerrainStoragePaths::Container(0);
	const TArray<uint8> Object = Pattern(512, 0x11);
	const FTerrainDigest Digest = TerrainPersistDigest(Object);

	TestFalse(TEXT("An absent object is absent"), Store.Contains(Digest));
	{
		TArray<uint8> Out;
		TestFalse(TEXT("and loading it fails"), Store.LoadObject(Digest, Out));
	}

	TestTrue(TEXT("Storing succeeds"), Store.StoreObject(Digest, Object));
	TestTrue(TEXT("and the object is now present"), Store.Contains(Digest));
	TestFalse(TEXT("NOT as a loose file -- that would be a new name at runtime (P-005)"),
		Device.Exists(TerrainStoragePaths::Object(Digest)));
	TestEqual(TEXT("and no file was created at all"), Device.NumFiles(), TerrainStoragePaths::ContainerCount);
	TestEqual(TEXT("It is one frame in c.0: header, object, one manifest entry, trailer"),
		Device.Size(C0), int64(TerrainFrameHeaderSize + 512 + TerrainPackEntrySize + TerrainPackTrailerSize));
	TestEqual(TEXT("and the valid end is the whole file"), Store.ContainerValidEnd(0), Device.Size(C0));

	{
		TArray<uint8> Out;
		TestTrue (TEXT("Loading succeeds"), Store.LoadObject(Digest, Out));
		TestTrue (TEXT("and returns the same bytes"), Out == Object);
	}

	// Content addressing is checked on the way IN.
	{
		const int64 Before = Device.Size(C0);
		const FTerrainDigest Wrong = MakeDigest(0x01);
		TestFalse(TEXT("Storing under a digest the bytes do not hash to is refused"),
			Store.StoreObject(Wrong, Object));
		TestEqual(TEXT("and nothing was written"), Device.Size(C0), Before);
	}

	// Storing the same object twice is a success that writes nothing.
	{
		const int64 Before = Device.Size(C0);
		TestTrue (TEXT("Storing the same object again succeeds"), Store.StoreObject(Digest, Object));
		TestEqual(TEXT("and writes nothing"), Device.Size(C0), Before);
	}

	// ...and on the way OUT: bit rot under the store is detected, not returned.
	{
		TArray<uint8>* OnDisk = Device.Find(C0);
		const int32 At = FindBytes(*OnDisk, Object);
		TestTrue(TEXT("The object's bytes are in the container"), At >= 0);
		(*OnDisk)[At + 100] ^= 0xFF;

		TArray<uint8> Out;
		TestFalse(TEXT("A damaged object fails to load rather than returning bad bytes"),
			Store.LoadObject(Digest, Out));

		(*OnDisk)[At + 100] ^= 0xFF;
		TestTrue(TEXT("and loads again once repaired"), Store.LoadObject(Digest, Out));
	}

	// A failed append leaves no object behind, and the store reports failure.
	{
		FTerrainFaultDevice Faulty(Device);
		FTerrainFileObjectStore FaultyStore(Faulty);
		FaultyStore.LoadPacks();

		const TArray<uint8> Second = Pattern(300, 0x77);
		const FTerrainDigest SecondDigest = TerrainPersistDigest(Second);

		Faulty.FailAfter(ETerrainStorageOp::Append, 0);
		TestFalse(TEXT("A failed append is reported as failure"),
			FaultyStore.StoreObject(SecondDigest, Second));
		TestFalse(TEXT("and leaves no object"), FaultyStore.Contains(SecondDigest));

		Faulty.ClearFaults();
		TestTrue(TEXT("and the retry succeeds"), FaultyStore.StoreObject(SecondDigest, Second));
		TestEqual(TEXT("No WriteNew happened on an open store"), Faulty.OpCount(ETerrainStorageOp::WriteNew), 0);
	}

	// A TORN append leaves bytes past the valid end. They must not load, and the next append must
	// cut them first -- a frame written after garbage would be invisible to the next scan.
	{
		FTerrainFaultDevice Faulty(Device);
		FTerrainFileObjectStore FaultyStore(Faulty);
		FaultyStore.LoadPacks();
		const int64 ValidBefore = FaultyStore.ContainerValidEnd(0);
		TestEqual(TEXT("The reopened store sees every frame"), ValidBefore, Device.Size(C0));

		const TArray<uint8> Third = Pattern(400, 0x33);
		const FTerrainDigest ThirdDigest = TerrainPersistDigest(Third);

		Faulty.TearAfter(ETerrainStorageOp::Append, 0, /*TearBytes=*/128);
		TestFalse(TEXT("A torn append is reported as failure"),
			FaultyStore.StoreObject(ThirdDigest, Third));
		TestEqual(TEXT("but the torn bytes are on disk, as after a crash"),
			Device.Size(C0), ValidBefore + 128);
		TArray<uint8> Out;
		TestFalse(TEXT("and nothing in them loads"), FaultyStore.LoadObject(ThirdDigest, Out));

		// A fresh process sees the tail as a tail, and mutates nothing by looking.
		{
			FTerrainFileObjectStore Scanner(Device);
			Scanner.LoadPacks();
			TestEqual(TEXT("A scan stops at the torn frame"), Scanner.ContainerValidEnd(0), ValidBefore);
			TestEqual(TEXT("and does not cut it"), Device.Size(C0), ValidBefore + 128);
		}

		// If the cut fails, the append must not happen.
		Faulty.ClearFaults();
		Faulty.FailAfter(ETerrainStorageOp::Truncate, 0);
		const int32 AppendsBefore = Faulty.OpCount(ETerrainStorageOp::Append);
		TestFalse(TEXT("With the torn tail uncut, the store refuses to append"),
			FaultyStore.StoreObject(ThirdDigest, Third));
		TestEqual(TEXT("and did not try"), Faulty.OpCount(ETerrainStorageOp::Append), AppendsBefore);

		Faulty.ClearFaults();
		TestTrue(TEXT("The retry succeeds"), FaultyStore.StoreObject(ThirdDigest, Third));
		TestTrue(TEXT("and loads"), FaultyStore.LoadObject(ThirdDigest, Out));

		FTerrainFileObjectStore Reopened(Device);
		Reopened.LoadPacks();
		TestEqual(TEXT("After the cut and the append, a scan sees the whole container"),
			Reopened.ContainerValidEnd(0), Device.Size(C0));
		TestTrue(TEXT("and every object"), Reopened.Contains(Digest) && Reopened.Contains(ThirdDigest));
	}

	// The self-offset rule: a whole, valid frame copied to a position it did not name is not a
	// frame. This is what keeps stale bytes beyond a truncation point from being believed.
	{
		FTerrainMemoryStorageDevice Moved;
		FTerrainFileObjectStore MovedStore(Moved);
		MovedStore.EnsureLayout();
		MovedStore.LoadPacks();

		const TArray<uint8> A = Pattern(64, 0x41);
		MovedStore.StoreObject(TerrainPersistDigest(A), A);
		TArray<uint8> Frame = *Moved.Find(TerrainStoragePaths::Container(0));

		TArray<uint8>* C1 = Moved.Find(TerrainStoragePaths::Container(1));
		C1->Append(Pattern(40, 0x00));   // a "torn" prefix of the same length as a header
		C1->Append(Frame);               // then a perfect frame that names offset 0, not 40

		FTerrainFileObjectStore Scanner(Moved);
		Scanner.LoadPacks();
		TestEqual(TEXT("A frame at an offset it does not name ends the scan"), Scanner.ContainerValidEnd(1), int64(0));
		TestEqual(TEXT("so c.1 contributes nothing"), Scanner.ContainerDeadBytes(1, TSet<FTerrainDigest>()), int64(0));
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

	// P-005's three operations, against the real file system: a positioned read, a shrink-only
	// truncation through an append handle, and a directory sync.
	{
		TArray<uint8> Whole, Range;
		Device.Read(Path, Whole);
		TestEqual(TEXT("ReadRange succeeds"), Device.ReadRange(Path, 990, 47, Range), ETerrainStorageResult::Ok);
		TestEqual(TEXT("and returns exactly that range"),
			FMemory::Memcmp(Range.GetData(), Whole.GetData() + 990, 47), 0);
		TestEqual(TEXT("A range past the end is WrongSize"),
			Device.ReadRange(Path, 1000, 38, Range), ETerrainStorageResult::WrongSize);

		TestEqual(TEXT("Truncate shrinks"), Device.Truncate(Path, 1000), ETerrainStorageResult::Ok);
		TestEqual(TEXT("to exactly the size asked"), Device.Size(Path), (int64)1000);
		TArray<uint8> Back;
		Device.Read(Path, Back);
		TestEqual(TEXT("keeping every byte before the cut"), FMemory::Memcmp(Back.GetData(), Whole.GetData(), 1000), 0);
		TestEqual(TEXT("Truncate refuses to grow a file"),
			Device.Truncate(Path, 1001), ETerrainStorageResult::WrongSize);
		TestEqual(TEXT("and the file is untouched"), Device.Size(Path), (int64)1000);
		TestEqual(TEXT("Truncating an absent file is NotFound"),
			Device.Truncate(TEXT("objects/ab/absent.tobj"), 0), ETerrainStorageResult::NotFound);

		TestEqual(TEXT("Appending after a truncation lands at the new end"),
			Device.Append(Path, Pattern(5, 0xC0)), ETerrainStorageResult::Ok);
		TestEqual(TEXT("so the file is 1005 bytes"), Device.Size(Path), (int64)1005);

		// On Windows this logs a warning if NTFS refuses the directory flush; the log is the
		// evidence for P-005 §6 either way.
		TestEqual(TEXT("SyncDirectory on a subdirectory"), Device.SyncDirectory(TEXT("objects/ab")), ETerrainStorageResult::Ok);
		TestEqual(TEXT("SyncDirectory on the root"), Device.SyncDirectory(FString()), ETerrainStorageResult::Ok);
		TestEqual(TEXT("SyncDirectory refuses an escaping path"),
			Device.SyncDirectory(TEXT("../escaped")), ETerrainStorageResult::BadPath);
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


// ---- packs: many objects, one durable write (P-004 §13) -----------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainStoragePackTest,
	"TerrainCore.Persistence.Storage.Container",
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

bool FTerrainStoragePackTest::RunTest(const FString& Parameters)
{
	using namespace TerrainStorageTest;

	const FString C0 = TerrainStoragePaths::Container(0);

	// --- a batch is one append to a file that already exists, and nothing is durable before it ---
	{
		FTerrainMemoryStorageDevice Memory;
		FTerrainFaultDevice Device(Memory);
		FTerrainFileObjectStore Store(Device);
		TestEqual(TEXT("Layout is created"), Store.EnsureLayout(), ETerrainStorageResult::Ok);
		Store.LoadPacks();
		const int32 WriteNewAfterLayout = Device.OpCount(ETerrainStorageOp::WriteNew);

		TArray<TArray<uint8>> Objects;
		TArray<FTerrainDigest> Digests;
		for (int32 Index = 0; Index < 12; ++Index)
		{
			Objects.Add(Pattern(150 + Index, static_cast<uint8>(0x20 + Index)));
			Digests.Add(TerrainPersistDigest(Objects.Last()));
		}

		Store.BeginBatch();
		TestTrue(TEXT("The batch is open"), Store.IsBatchOpen());
		for (int32 Index = 0; Index < Objects.Num(); ++Index)
		{
			TestTrue(TEXT("Storing into a batch succeeds"),
				Store.StoreObject(Digests[Index], Objects[Index]));
		}
		TestEqual(TEXT("Everything is buffered"), Store.BatchNum(), 12);

		// The index path-copy re-reads pages it wrote moments earlier, before anything is durable.
		TArray<uint8> Readback;
		TestTrue(TEXT("A buffered object reads back before commit"),
			Store.LoadObject(Digests[5], Readback));
		TestTrue(TEXT("and reads back exactly"), Readback == Objects[5]);
		TestEqual(TEXT("Nothing is on disk before commit"), Memory.Size(C0), int64(0));

		TestEqual(TEXT("Commit writes the frame"), Store.CommitBatch(), ETerrainStorageResult::Ok);
		TestFalse(TEXT("and closes the batch"), Store.IsBatchOpen());
		TestEqual(TEXT("Twelve objects became ONE append"), Device.OpCount(ETerrainStorageOp::Append), 1);
		TestEqual(TEXT("and no new name"), Device.OpCount(ETerrainStorageOp::WriteNew), WriteNewAfterLayout);
		TestEqual(TEXT("The file count is still the pool"), Memory.NumFiles(), TerrainStoragePaths::ContainerCount);

		// A fresh store over the same device must find them all: a container is a store, not a cache.
		FTerrainFileObjectStore Reopened(Memory);
		TestEqual(TEXT("Containers load"), Reopened.LoadPacks(), ETerrainStorageResult::Ok);
		TestEqual(TEXT("All twelve are mapped"), Reopened.NumPackedObjects(), 12);
		TestEqual(TEXT("and writing continues where it left off"), Reopened.GetActiveContainer(), 0);
		for (int32 Index = 0; Index < Objects.Num(); ++Index)
		{
			TArray<uint8> Loaded;
			TestTrue(TEXT("A framed object loads after reopen"),
				Reopened.LoadObject(Digests[Index], Loaded));
			TestTrue(TEXT("with the exact bytes"), Loaded == Objects[Index]);
			TestTrue(TEXT("and Contains agrees"), Reopened.Contains(Digests[Index]));
		}
	}

	// --- an abandoned batch leaves the store exactly as it found it --------------------------
	{
		FTerrainMemoryStorageDevice Device;
		FTerrainFileObjectStore Store(Device);
		Store.EnsureLayout();
		Store.LoadPacks();

		const TArray<uint8> Object = Pattern(300, 0x77);
		const FTerrainDigest Digest = TerrainPersistDigest(Object);

		Store.BeginBatch();
		TestTrue(TEXT("Stored into the batch"), Store.StoreObject(Digest, Object));
		Store.AbandonBatch();

		TestFalse(TEXT("An abandoned batch stored nothing"), Store.Contains(Digest));
		TestEqual(TEXT("and wrote nothing"), Device.Size(C0), int64(0));
	}

	// --- a torn frame ends the scan; the frames before it are whole ---------------------------
	// A frame is flushed before the root slot that names it, so a torn one belongs to a capture
	// that never published. Refusing to open over it would turn garbage into a dead world.
	{
		FTerrainMemoryStorageDevice Device;
		FTerrainFileObjectStore Store(Device);
		Store.EnsureLayout();
		Store.LoadPacks();

		const TArray<uint8> Good = Pattern(200, 0x01);
		const FTerrainDigest GoodDigest = TerrainPersistDigest(Good);
		TestTrue(TEXT("First frame commits"), Store.StoreObject(GoodDigest, Good));
		const int64 FirstEnd = Device.Size(C0);

		const TArray<uint8> Lost = Pattern(200, 0x02);
		const FTerrainDigest LostDigest = TerrainPersistDigest(Lost);
		TestTrue(TEXT("Second frame commits"), Store.StoreObject(LostDigest, Lost));

		// Tear the second frame's trailer off -- exactly what a crash mid-append leaves behind.
		// Its header still claims the full length, which no longer fits: the header fails.
		Device.Find(C0)->SetNum(Device.Size(C0) - 8);

		FTerrainFileObjectStore Reopened(Device);
		TestEqual(TEXT("Opening over a torn frame succeeds"),
			Reopened.LoadPacks(), ETerrainStorageResult::Ok);
		TestEqual(TEXT("The valid end is the end of the first frame"), Reopened.ContainerValidEnd(0), FirstEnd);
		TestEqual(TEXT("The intact frame still resolves"), Reopened.NumPackedObjects(), 1);
		TestTrue(TEXT("and its object loads"), Reopened.Contains(GoodDigest));
		TestFalse(TEXT("The torn frame's object does not"), Reopened.Contains(LostDigest));

		// The next frame goes where the torn one started, not after it.
		const TArray<uint8> Next = Pattern(200, 0x03);
		const FTerrainDigest NextDigest = TerrainPersistDigest(Next);
		TestTrue(TEXT("A new frame commits over the torn tail"), Reopened.StoreObject(NextDigest, Next));

		FTerrainFileObjectStore Again(Device);
		Again.LoadPacks();
		TestEqual(TEXT("and a later scan reaches the end of the file"), Again.ContainerValidEnd(0), Device.Size(C0));
		TestTrue(TEXT("finding the new object"), Again.Contains(NextDigest));
		TestFalse(TEXT("and still not the torn one"), Again.Contains(LostDigest));
	}

	// --- a corrupt body is skipped wholesale, and the frames after it still count --------------
	{
		FTerrainMemoryStorageDevice Device;
		FTerrainFileObjectStore Store(Device);
		Store.EnsureLayout();
		Store.LoadPacks();

		const TArray<uint8> First  = Pattern(400, 0x5A);
		const TArray<uint8> Second = Pattern(400, 0x5B);
		Store.StoreObject(TerrainPersistDigest(First), First);
		Store.StoreObject(TerrainPersistDigest(Second), Second);

		(*Device.Find(C0))[TerrainFrameHeaderSize + 10] ^= 0xFF;   // one flipped bit in frame 1's body

		FTerrainFileObjectStore Reopened(Device);
		TestEqual(TEXT("Opening over a corrupt frame succeeds"),
			Reopened.LoadPacks(), ETerrainStorageResult::Ok);
		TestFalse(TEXT("nothing in it is trusted"), Reopened.Contains(TerrainPersistDigest(First)));
		TestTrue(TEXT("but the frame after it is still found"), Reopened.Contains(TerrainPersistDigest(Second)));
		TestEqual(TEXT("and the valid end covers both"), Reopened.ContainerValidEnd(0), Device.Size(C0));
	}

	// --- pre-P-005 loose objects and packs are still read, and never written -----------------
	{
		FTerrainMemoryStorageDevice Device;

		const TArray<uint8> Loose = Pattern(100, 0xA1);
		const FTerrainDigest LooseDigest = TerrainPersistDigest(Loose);
		Device.EnsureDirectory(TerrainStoragePaths::ObjectDirectory(LooseDigest));
		Device.WriteNew(TerrainStoragePaths::Object(LooseDigest), Loose);

		const TArray<uint8> Packed = Pattern(100, 0xA2);
		const FTerrainDigest PackedDigest = TerrainPersistDigest(Packed);
		Device.EnsureDirectory(TerrainStoragePaths::PacksDirectory);
		Device.WriteNew(TerrainStoragePaths::Pack(0), MakeLegacyPackImage({ Packed }));

		FTerrainFileObjectStore Store(Device);
		Store.EnsureLayout();
		Store.LoadPacks();
		TArray<uint8> A, B;
		TestTrue(TEXT("A pre-P-005 loose object loads"), Store.LoadObject(LooseDigest, A));
		TestTrue(TEXT("A pre-P-005 packed object loads"), Store.LoadObject(PackedDigest, B));
		TestTrue(TEXT("Loose bytes are exact"), A == Loose);
		TestTrue(TEXT("Packed bytes are exact"), B == Packed);

		// Re-storing something already held is a success that writes nothing, wherever it is held.
		Store.BeginBatch();
		TestTrue(TEXT("Re-storing a legacy packed object succeeds"), Store.StoreObject(PackedDigest, Packed));
		TestTrue(TEXT("Re-storing a legacy loose object succeeds"), Store.StoreObject(LooseDigest, Loose));
		TestEqual(TEXT("and buffers nothing"), Store.BatchNum(), 0);
		Store.AbandonBatch();
	}

	// --- a digest that does not match its bytes is refused, batched or not -------------------
	{
		FTerrainMemoryStorageDevice Device;
		FTerrainFileObjectStore Store(Device);
		Store.EnsureLayout();

		const TArray<uint8> Object = Pattern(64, 0x0B);
		FTerrainDigest Wrong = TerrainPersistDigest(Object);
		Wrong.Bytes[0] ^= 0x01;

		Store.BeginBatch();
		TestFalse(TEXT("A mismatched digest is refused in a batch"),
			Store.StoreObject(Wrong, Object));
		TestEqual(TEXT("and buffers nothing"), Store.BatchNum(), 0);
		Store.AbandonBatch();
	}

	// --- a store whose pool was never created refuses rather than creating it -----------------
	{
		FTerrainMemoryStorageDevice Device;
		FTerrainFileObjectStore Store(Device);
		const TArray<uint8> Object = Pattern(64, 0x0C);
		TestFalse(TEXT("Without bootstrap, storing fails"), Store.StoreObject(TerrainPersistDigest(Object), Object));
		TestEqual(TEXT("and no name was created"), Device.NumFiles(), 0);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "Misc/AutomationTest.h"
#include "TerrainStorage.h"
#include "TerrainWorldStore.h"
#include "TerrainPersistenceFixtures.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The exclusive writer lease (P-003 §5, T-128).
 *
 * The assertion that matters is not "the second acquire returned Busy" -- a stub that always
 * refused would pass that. It is the pair: the second holder is refused **while** the first
 * holds, the world's bytes are identical before and after the refusal, and the same second
 * holder succeeds the moment the first lets go. Each half checks the other actually varied.
 *
 * The cross-PROCESS half -- two real servers, one killed hard -- is `Tools/Test-TerrainLease.py`,
 * because a lock that only excludes itself inside one process proves nothing about the thing
 * P-003 is afraid of.
 */

namespace TerrainWriterLeaseTest
{
	/** Every file on a memory device, by path, with its bytes. */
	TMap<FString, TArray<uint8>> Snapshot(FTerrainMemoryStorageDevice& Device)
	{
		TMap<FString, TArray<uint8>> Out;
		TArray<FString> Paths;
		Device.GetPaths(Paths);
		for (const FString& Path : Paths)
		{
			Out.Add(Path, *Device.Find(Path));
		}
		return Out;
	}

	/** Every file under a real directory, relative path -> bytes. */
	TMap<FString, TArray<uint8>> Snapshot(const FString& Root)
	{
		TMap<FString, TArray<uint8>> Out;
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*"), /*Files=*/true, /*Directories=*/false);
		for (const FString& File : Files)
		{
			FString Relative = File;
			FPaths::MakePathRelativeTo(Relative, *(Root / TEXT("")));
			TArray<uint8> Bytes;
			// The lock file's byte 0 is locked while held, so reading it would contend with the
			// very lock under test. Its presence is recorded; its length is checked below.
			if (Relative != TerrainStoragePaths::WriterLock)
			{
				FFileHelper::LoadFileToArray(Bytes, *File);
			}
			Out.Add(Relative, MoveTemp(Bytes));
		}
		return Out;
	}

	bool SameFiles(const TMap<FString, TArray<uint8>>& A, const TMap<FString, TArray<uint8>>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (const TPair<FString, TArray<uint8>>& Entry : A)
		{
			const TArray<uint8>* Other = B.Find(Entry.Key);
			if (!Other || *Other != Entry.Value)
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainWriterLeaseTest,
	"TerrainCore.Persistence.Storage.WriterLease",
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

bool FTerrainWriterLeaseTest::RunTest(const FString& Parameters)
{
	using namespace TerrainWriterLeaseTest;
	using namespace TerrainPersistTest;

	const FTerrainPersistIdentity Identity = MakeIdentity();
	const FTerrainBaseDescriptor  Base     = MakeBaseDescriptor();

	// ===== memory device: the contract, headless ==============================================
	{
		FTerrainMemoryStorageDevice Memory;

		TMap<FString, TArray<uint8>> Written;
		{
			FTerrainWorldStore First(Memory);
			TestFalse(TEXT("A new store holds no lease"), First.HoldsWriterLease());
			TestTrue (TEXT("The first holder takes the lease"), First.AcquireWriterLease().IsOk());
			TestTrue (TEXT("and holds it"), First.HoldsWriterLease());
			TestTrue (TEXT("Acquiring again is idempotent"), First.AcquireWriterLease().IsOk());
			TestTrue (TEXT("The lock file exists"), Memory.Exists(TerrainStoragePaths::WriterLock));
			if (!First.Create(Base, Identity.World, Identity.Epoch, 1789412345678LL).IsOk())
			{
				AddError(TEXT("Create failed under the lease"));
				return false;
			}
			Written = Snapshot(Memory);

			// A second store over the same device: the second server.
			FTerrainWorldStore Second(Memory);
			const FTerrainStoreResult Refused = Second.AcquireWriterLease();
			TestEqual(TEXT("A second holder is refused as StoreBusy"), Refused.Storage, ETerrainStorageResult::Busy);
			TestFalse(TEXT("and holds nothing"), Second.HoldsWriterLease());
			TestTrue (TEXT("and the refusal changed no byte of the world"), SameFiles(Written, Snapshot(Memory)));

			// Through a decorator, as the crash matrix wraps devices: the lease is the disk's.
			FTerrainFaultDevice Wrapped(Memory);
			FTerrainWorldStore Third(Wrapped);
			TestEqual(TEXT("A holder through a wrapping device is refused too"),
				Third.AcquireWriterLease().Storage, ETerrainStorageResult::Busy);

			// Refusal is not a fault: the first holder keeps working.
			TestTrue(TEXT("The first holder still publishes after a refusal"),
				First.PublishCheckpoint(First.GetState().Checkpoint, 1789412345679LL).IsOk());
			Written = Snapshot(Memory);
		}

		TestFalse(TEXT("Destroying the holder releases the lease"), Memory.IsLeaseHeld(TerrainStoragePaths::WriterLock));
		TestTrue (TEXT("and releasing it touched no byte"), SameFiles(Written, Snapshot(Memory)));

		FTerrainWorldStore Next(Memory);
		TestTrue(TEXT("The next holder takes the released lease"), Next.AcquireWriterLease().IsOk());
		TestTrue(TEXT("and opens the world the first one wrote"), Next.Open().IsOk());
		TestEqual(TEXT("at the generation the first one published"), Next.GetState().Root.Generation, (uint64)2);

		// A failed acquisition is reported and leaves nothing held.
		FTerrainMemoryStorageDevice Other;
		FTerrainFaultDevice Faulty(Other);
		Faulty.FailAfter(ETerrainStorageOp::AcquireLease, 0);
		FTerrainWorldStore Failing(Faulty);
		TestEqual(TEXT("A device failure is IoError, not Busy"),
			Failing.AcquireWriterLease().Storage, ETerrainStorageResult::IoError);
		TestFalse(TEXT("and holds nothing"), Failing.HoldsWriterLease());
		TestFalse(TEXT("and leaves no lease on the device"), Other.IsLeaseHeld(TerrainStoragePaths::WriterLock));
		TestEqual(TEXT("It does not count as a mutation"), Faulty.MutationCount(), 0);
	}

	// ===== the real file system: the OS lock itself ============================================
	{
		const FString Root = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectSavedDir() / TEXT("Automation") / TEXT("TerrainWriterLeaseTest"));
		IFileManager::Get().DeleteDirectory(*Root, /*RequireExists=*/false, /*Tree=*/true);
		ON_SCOPE_EXIT
		{
			IFileManager::Get().DeleteDirectory(*Root, /*RequireExists=*/false, /*Tree=*/true);
		};

		// Two devices over one directory: two servers' views of the same disk.
		FTerrainPlatformStorageDevice DeviceA(Root);
		FTerrainPlatformStorageDevice DeviceB(Root);

		TMap<FString, TArray<uint8>> Written;
		{
			FTerrainWorldStore First(DeviceA);
			TestTrue(TEXT("Disk: the lease is taken before the world directory exists"),
				First.AcquireWriterLease().IsOk());
			TestTrue(TEXT("Disk: and it created the directory and the lock file"),
				FPaths::FileExists(Root / TerrainStoragePaths::WriterLock));
			if (!First.Create(Base, Identity.World, Identity.Epoch, 1789412345678LL).IsOk())
			{
				AddError(TEXT("Disk: create failed under the lease"));
				return false;
			}
			Written = Snapshot(Root);

			FTerrainWorldStore Second(DeviceB);
			TestEqual(TEXT("Disk: a second device on the same directory is refused as StoreBusy"),
				Second.AcquireWriterLease().Storage, ETerrainStorageResult::Busy);
			FTerrainWorldStore SameDevice(DeviceA);
			TestEqual(TEXT("Disk: a second store on the same device is refused too"),
				SameDevice.AcquireWriterLease().Storage, ETerrainStorageResult::Busy);
			TestTrue(TEXT("Disk: the refusals changed no byte of the world"), SameFiles(Written, Snapshot(Root)));

#if PLATFORM_WINDOWS
			// No FILE_SHARE_DELETE: nobody can remove the lock out from under its holder.
			TestFalse(TEXT("Disk: the held lock file cannot be deleted"),
				FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*(Root / TerrainStoragePaths::WriterLock)));
#endif
		}

		FTerrainWorldStore Second(DeviceB);
		TestTrue(TEXT("Disk: the second device takes the lease once the first is released"),
			Second.AcquireWriterLease().IsOk());
		TestTrue(TEXT("Disk: and opens the world"), Second.Open().IsOk());
		TestTrue(TEXT("Disk: an existing lock file is reused, not rewritten"), SameFiles(Written, Snapshot(Root)));
		TestEqual(TEXT("Disk: the lock file holds no bytes; only the lock means anything"),
			IFileManager::Get().FileSize(*(Root / TerrainStoragePaths::WriterLock)), (int64)0);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

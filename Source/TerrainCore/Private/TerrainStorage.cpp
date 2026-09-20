// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainStorage.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/Paths.h"

/**
 * The storage device seam and the two stores built on it (P-004 §11, §12).
 *
 * THE FLUSH RULE, stated once and obeyed everywhere below: every mutating operation ends with
 * `Flush(true)` before the handle is closed, and returns IoError if that flush fails.
 *
 * `Flush`'s default argument is `false`. On Windows the parameter is ignored and the call is
 * always `FlushFileBuffers`. On Unix it is not ignored: `false` is `fdatasync`, `true` is
 * `fsync`, and `fdatasync` makes no promise about **metadata** -- including the file's new
 * length. Every WriteNew and every Append extends a file, and the Linux dedicated server is
 * the shipping target (D-002), so a defaulted `Flush()` would be correct on the development
 * machine and wrong on the shipping one.
 */

const TCHAR* TerrainStorageResultName(ETerrainStorageResult Result)
{
	switch (Result)
	{
	case ETerrainStorageResult::Ok:            return TEXT("Ok");
	case ETerrainStorageResult::NotFound:      return TEXT("NotFound");
	case ETerrainStorageResult::AlreadyExists: return TEXT("AlreadyExists");
	case ETerrainStorageResult::BadPath:       return TEXT("BadPath");
	case ETerrainStorageResult::WrongSize:     return TEXT("WrongSize");
	case ETerrainStorageResult::IoError:       return TEXT("IoError");
	}
	return TEXT("Unknown");
}

// ---- path rules ---------------------------------------------------------

bool TerrainStorageIsSafeRelativePath(const FString& RelativePath)
{
	if (RelativePath.IsEmpty() || RelativePath.Len() > 512)
	{
		return false;
	}

	// No backslashes, no drive letters, no leading separator: all three are ways to leave the
	// root on Windows, and two of them are ways to leave it on Unix.
	if (RelativePath.Contains(TEXT("\\")) || RelativePath.Contains(TEXT(":")))
	{
		return false;
	}
	if (RelativePath[0] == TEXT('/') || RelativePath[RelativePath.Len() - 1] == TEXT('/'))
	{
		return false;
	}
	if (RelativePath.Contains(TEXT("//")))
	{
		return false;
	}

	TArray<FString> Components;
	RelativePath.ParseIntoArray(Components, TEXT("/"), /*InCullEmpty=*/false);
	for (const FString& Component : Components)
	{
		if (Component.IsEmpty() || Component == TEXT(".") || Component == TEXT(".."))
		{
			return false;
		}
		for (int32 Index = 0; Index < Component.Len(); ++Index)
		{
			const TCHAR Char = Component[Index];
			const bool bAllowed =
				   (Char >= TEXT('a') && Char <= TEXT('z'))
				|| (Char >= TEXT('A') && Char <= TEXT('Z'))
				|| (Char >= TEXT('0') && Char <= TEXT('9'))
				||  Char == TEXT('.') || Char == TEXT('-') || Char == TEXT('_');
			if (!bAllowed)
			{
				// A restricted alphabet rather than a blocklist. Every name this project
				// writes is generated from a digest, an index or a fixed literal, so nothing
				// legitimate needs a character outside it.
				return false;
			}
		}
	}
	return true;
}

namespace TerrainStoragePaths
{
	FString RootSlot(int32 SlotIndex)
	{
		check(SlotIndex == 0 || SlotIndex == 1);
		return FString::Printf(TEXT("%s/root.%d"), RootsDirectory, SlotIndex);
	}

	FString AnchorSlot(int32 SlotIndex)
	{
		check(SlotIndex == 0 || SlotIndex == 1);
		return FString::Printf(TEXT("%s/anchor.%d"), JournalDirectory, SlotIndex);
	}

	FString JournalSegment(uint64 SegmentId)
	{
		return FString::Printf(TEXT("%s/seg-%016llx.tjs"), JournalDirectory, SegmentId);
	}

	bool ParseJournalSegment(const FString& FileName, uint64& OutSegmentId)
	{
		static const FString SegmentPrefix = TEXT("seg-");
		static const FString SegmentSuffix = TEXT(".tjs");

		if (!FileName.StartsWith(SegmentPrefix) || !FileName.EndsWith(SegmentSuffix)
			|| FileName.Len() - SegmentPrefix.Len() - SegmentSuffix.Len() != 16)
		{
			return false;
		}

		uint64 Value = 0;
		for (int32 Index = 0; Index < 16; ++Index)
		{
			const TCHAR Char = FileName[SegmentPrefix.Len() + Index];
			uint64 Nibble;
			if      (Char >= TEXT('0') && Char <= TEXT('9')) { Nibble = static_cast<uint64>(Char - TEXT('0')); }
			else if (Char >= TEXT('a') && Char <= TEXT('f')) { Nibble = static_cast<uint64>(Char - TEXT('a') + 10); }
			else { return false; }   // lowercase only, matching what JournalSegment writes
			Value = (Value << 4) | Nibble;
		}
		OutSegmentId = Value;
		return true;
	}

	FString ObjectDirectory(const FTerrainDigest& Digest)
	{
		// The first digest byte, lowercase hex: a 256-way fan-out so one directory never holds
		// millions of entries.
		return FString::Printf(TEXT("%s/%02x"), ObjectsDirectory, Digest.Bytes[0]);
	}

	FString Object(const FTerrainDigest& Digest)
	{
		return FString::Printf(TEXT("%s/%s.tobj"),
			*ObjectDirectory(Digest), *TerrainPersistDigestToHex(Digest));
	}
}

// ---- the platform device ------------------------------------------------

namespace
{
	/** Write, then flush with the strong guarantee. See the file header comment. */
	bool WriteAndFlush(IFileHandle& Handle, TArrayView<const uint8> Bytes)
	{
		if (Bytes.Num() > 0 && !Handle.Write(Bytes.GetData(), Bytes.Num()))
		{
			return false;
		}
		return Handle.Flush(/*bFullFlush=*/true);
	}
}

FTerrainPlatformStorageDevice::FTerrainPlatformStorageDevice(const FString& InAbsoluteRoot)
	: Root(InAbsoluteRoot)
{
	FPaths::NormalizeDirectoryName(Root);
}

FString FTerrainPlatformStorageDevice::Resolve(const FString& RelativePath) const
{
	if (!TerrainStorageIsSafeRelativePath(RelativePath))
	{
		return FString();
	}
	return Root / RelativePath;
}

ETerrainStorageResult FTerrainPlatformStorageDevice::EnsureDirectory(const FString& RelativePath)
{
	const FString Absolute = Resolve(RelativePath);
	if (Absolute.IsEmpty())
	{
		return ETerrainStorageResult::BadPath;
	}

	IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
	if (File.DirectoryExists(*Absolute))
	{
		return ETerrainStorageResult::Ok;
	}
	return File.CreateDirectoryTree(*Absolute)
		? ETerrainStorageResult::Ok : ETerrainStorageResult::IoError;
}

bool FTerrainPlatformStorageDevice::Exists(const FString& RelativePath) const
{
	const FString Absolute = Resolve(RelativePath);
	return !Absolute.IsEmpty() && FPlatformFileManager::Get().GetPlatformFile().FileExists(*Absolute);
}

int64 FTerrainPlatformStorageDevice::Size(const FString& RelativePath) const
{
	const FString Absolute = Resolve(RelativePath);
	if (Absolute.IsEmpty())
	{
		return -1;
	}
	return FPlatformFileManager::Get().GetPlatformFile().FileSize(*Absolute);
}

ETerrainStorageResult FTerrainPlatformStorageDevice::Read(const FString& RelativePath, TArray<uint8>& OutBytes) const
{
	const FString Absolute = Resolve(RelativePath);
	if (Absolute.IsEmpty())
	{
		return ETerrainStorageResult::BadPath;
	}

	IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> Handle(File.OpenRead(*Absolute));
	if (!Handle.IsValid())
	{
		return File.FileExists(*Absolute) ? ETerrainStorageResult::IoError : ETerrainStorageResult::NotFound;
	}

	const int64 FileSize = Handle->Size();
	if (FileSize < 0 || FileSize > MAX_int32)
	{
		return ETerrainStorageResult::IoError;
	}

	OutBytes.Reset();
	OutBytes.AddUninitialized(static_cast<int32>(FileSize));
	if (FileSize > 0 && !Handle->Read(OutBytes.GetData(), FileSize))
	{
		OutBytes.Reset();
		return ETerrainStorageResult::IoError;
	}
	return ETerrainStorageResult::Ok;
}

ETerrainStorageResult FTerrainPlatformStorageDevice::ListFiles(
	const FString& RelativeDirectory, TArray<FString>& OutNames) const
{
	const FString Absolute = Resolve(RelativeDirectory);
	if (Absolute.IsEmpty())
	{
		return ETerrainStorageResult::BadPath;
	}

	IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
	if (!File.DirectoryExists(*Absolute))
	{
		// Not an empty list: "there is no journal directory" and "the journal directory is
		// empty" are different facts about a world.
		return ETerrainStorageResult::NotFound;
	}

	OutNames.Reset();
	File.IterateDirectory(*Absolute,
		[&OutNames](const TCHAR* Path, bool bIsDirectory) -> bool
		{
			if (!bIsDirectory)
			{
				OutNames.Add(FPaths::GetCleanFilename(Path));
			}
			return true;
		});

	// Sorted, so discovery does not depend on the order the file system happens to report.
	OutNames.Sort();
	return ETerrainStorageResult::Ok;
}

ETerrainStorageResult FTerrainPlatformStorageDevice::WriteNew(const FString& RelativePath, TArrayView<const uint8> Bytes)
{
	const FString Absolute = Resolve(RelativePath);
	if (Absolute.IsEmpty())
	{
		return ETerrainStorageResult::BadPath;
	}

	IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
	if (File.FileExists(*Absolute))
	{
		return ETerrainStorageResult::AlreadyExists;
	}

	TUniquePtr<IFileHandle> Handle(File.OpenWrite(*Absolute, /*bAppend=*/false, /*bAllowRead=*/true));
	if (!Handle.IsValid())
	{
		return ETerrainStorageResult::IoError;
	}
	return WriteAndFlush(*Handle, Bytes) ? ETerrainStorageResult::Ok : ETerrainStorageResult::IoError;
}

ETerrainStorageResult FTerrainPlatformStorageDevice::OverwriteInPlace(
	const FString& RelativePath, TArrayView<const uint8> Bytes)
{
	const FString Absolute = Resolve(RelativePath);
	if (Absolute.IsEmpty())
	{
		return ETerrainStorageResult::BadPath;
	}

	IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
	if (!File.FileExists(*Absolute))
	{
		return ETerrainStorageResult::NotFound;
	}
	if (File.FileSize(*Absolute) != Bytes.Num())
	{
		return ETerrainStorageResult::WrongSize;
	}

	// bAppend = true, then Seek(0). Opening with bAppend = false TRUNCATES, which would make
	// the slot briefly zero-length -- and a slot that can be absent breaks the one guarantee
	// the two-slot protocol rests on.
	TUniquePtr<IFileHandle> Handle(File.OpenWrite(*Absolute, /*bAppend=*/true, /*bAllowRead=*/true));
	if (!Handle.IsValid() || !Handle->Seek(0))
	{
		return ETerrainStorageResult::IoError;
	}
	return WriteAndFlush(*Handle, Bytes) ? ETerrainStorageResult::Ok : ETerrainStorageResult::IoError;
}

ETerrainStorageResult FTerrainPlatformStorageDevice::Append(const FString& RelativePath, TArrayView<const uint8> Bytes)
{
	const FString Absolute = Resolve(RelativePath);
	if (Absolute.IsEmpty())
	{
		return ETerrainStorageResult::BadPath;
	}

	IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
	if (!File.FileExists(*Absolute))
	{
		return ETerrainStorageResult::NotFound;
	}

	TUniquePtr<IFileHandle> Handle(File.OpenWrite(*Absolute, /*bAppend=*/true, /*bAllowRead=*/true));
	if (!Handle.IsValid() || !Handle->SeekFromEnd(0))
	{
		return ETerrainStorageResult::IoError;
	}
	return WriteAndFlush(*Handle, Bytes) ? ETerrainStorageResult::Ok : ETerrainStorageResult::IoError;
}

ETerrainStorageResult FTerrainPlatformStorageDevice::Delete(const FString& RelativePath)
{
	const FString Absolute = Resolve(RelativePath);
	if (Absolute.IsEmpty())
	{
		return ETerrainStorageResult::BadPath;
	}

	IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
	if (!File.FileExists(*Absolute))
	{
		return ETerrainStorageResult::NotFound;
	}
	return File.DeleteFile(*Absolute) ? ETerrainStorageResult::Ok : ETerrainStorageResult::IoError;
}

// ---- the memory device --------------------------------------------------

ETerrainStorageResult FTerrainMemoryStorageDevice::EnsureDirectory(const FString& RelativePath)
{
	if (!TerrainStorageIsSafeRelativePath(RelativePath))
	{
		return ETerrainStorageResult::BadPath;
	}
	Directories.Add(RelativePath);
	return ETerrainStorageResult::Ok;
}

bool FTerrainMemoryStorageDevice::Exists(const FString& RelativePath) const
{
	return Files.Contains(RelativePath);
}

int64 FTerrainMemoryStorageDevice::Size(const FString& RelativePath) const
{
	const TArray<uint8>* Found = Files.Find(RelativePath);
	return Found != nullptr ? Found->Num() : -1;
}

ETerrainStorageResult FTerrainMemoryStorageDevice::Read(const FString& RelativePath, TArray<uint8>& OutBytes) const
{
	if (!TerrainStorageIsSafeRelativePath(RelativePath))
	{
		return ETerrainStorageResult::BadPath;
	}
	const TArray<uint8>* Found = Files.Find(RelativePath);
	if (Found == nullptr)
	{
		return ETerrainStorageResult::NotFound;
	}
	OutBytes = *Found;
	return ETerrainStorageResult::Ok;
}

ETerrainStorageResult FTerrainMemoryStorageDevice::ListFiles(
	const FString& RelativeDirectory, TArray<FString>& OutNames) const
{
	if (!TerrainStorageIsSafeRelativePath(RelativeDirectory))
	{
		return ETerrainStorageResult::BadPath;
	}
	if (!Directories.Contains(RelativeDirectory))
	{
		return ETerrainStorageResult::NotFound;
	}

	OutNames.Reset();
	const FString Prefix = RelativeDirectory + TEXT("/");
	for (const TPair<FString, TArray<uint8>>& Pair : Files)
	{
		if (!Pair.Key.StartsWith(Prefix))
		{
			continue;
		}
		const FString Remainder = Pair.Key.RightChop(Prefix.Len());
		if (!Remainder.Contains(TEXT("/")))   // direct children only, never recursive
		{
			OutNames.Add(Remainder);
		}
	}
	OutNames.Sort();
	return ETerrainStorageResult::Ok;
}

ETerrainStorageResult FTerrainMemoryStorageDevice::WriteNew(const FString& RelativePath, TArrayView<const uint8> Bytes)
{
	if (!TerrainStorageIsSafeRelativePath(RelativePath))
	{
		return ETerrainStorageResult::BadPath;
	}
	if (Files.Contains(RelativePath))
	{
		return ETerrainStorageResult::AlreadyExists;
	}

	TArray<uint8>& Slot = Files.Add(RelativePath);
	Slot.Append(Bytes.GetData(), Bytes.Num());
	return ETerrainStorageResult::Ok;
}

ETerrainStorageResult FTerrainMemoryStorageDevice::OverwriteInPlace(
	const FString& RelativePath, TArrayView<const uint8> Bytes)
{
	if (!TerrainStorageIsSafeRelativePath(RelativePath))
	{
		return ETerrainStorageResult::BadPath;
	}
	TArray<uint8>* Found = Files.Find(RelativePath);
	if (Found == nullptr)
	{
		return ETerrainStorageResult::NotFound;
	}
	if (Found->Num() != Bytes.Num())
	{
		return ETerrainStorageResult::WrongSize;
	}
	FMemory::Memcpy(Found->GetData(), Bytes.GetData(), Bytes.Num());
	return ETerrainStorageResult::Ok;
}

ETerrainStorageResult FTerrainMemoryStorageDevice::Append(const FString& RelativePath, TArrayView<const uint8> Bytes)
{
	if (!TerrainStorageIsSafeRelativePath(RelativePath))
	{
		return ETerrainStorageResult::BadPath;
	}
	TArray<uint8>* Found = Files.Find(RelativePath);
	if (Found == nullptr)
	{
		return ETerrainStorageResult::NotFound;
	}
	Found->Append(Bytes.GetData(), Bytes.Num());
	return ETerrainStorageResult::Ok;
}

ETerrainStorageResult FTerrainMemoryStorageDevice::Delete(const FString& RelativePath)
{
	if (!TerrainStorageIsSafeRelativePath(RelativePath))
	{
		return ETerrainStorageResult::BadPath;
	}
	return Files.Remove(RelativePath) > 0 ? ETerrainStorageResult::Ok : ETerrainStorageResult::NotFound;
}

// ---- fault injection ----------------------------------------------------

void FTerrainFaultDevice::FailAfter(ETerrainStorageOp Op, int32 CountBefore, const FString& PathFilter)
{
	FFault& Fault = Faults[static_cast<int32>(Op)];
	Fault.Remaining  = CountBefore;
	Fault.TearBytes  = -1;
	Fault.PathFilter = PathFilter;
}

void FTerrainFaultDevice::TearAfter(ETerrainStorageOp Op, int32 CountBefore, int32 TearBytes, const FString& PathFilter)
{
	FFault& Fault = Faults[static_cast<int32>(Op)];
	Fault.Remaining  = CountBefore;
	Fault.TearBytes  = TearBytes;
	Fault.PathFilter = PathFilter;
}

void FTerrainFaultDevice::ClearFaults()
{
	for (int32 Index = 0; Index < static_cast<int32>(ETerrainStorageOp::Count); ++Index)
	{
		Faults[Index] = FFault();
	}
}

bool FTerrainFaultDevice::ShouldFail(ETerrainStorageOp Op, const FString& Path, int32& OutTearBytes)
{
	++Counts[static_cast<int32>(Op)];

	FFault& Fault = Faults[static_cast<int32>(Op)];
	if (Fault.Remaining < 0)
	{
		return false;
	}
	if (!Fault.PathFilter.IsEmpty() && Fault.PathFilter != Path)
	{
		return false;
	}
	if (Fault.Remaining > 0)
	{
		--Fault.Remaining;
		return false;
	}

	OutTearBytes = Fault.TearBytes;
	Fault.Remaining = -1;   // one-shot
	return true;
}

ETerrainStorageResult FTerrainFaultDevice::EnsureDirectory(const FString& RelativePath)
{
	int32 Tear = -1;
	if (ShouldFail(ETerrainStorageOp::EnsureDirectory, RelativePath, Tear))
	{
		return ETerrainStorageResult::IoError;
	}
	return Inner.EnsureDirectory(RelativePath);
}

bool  FTerrainFaultDevice::Exists(const FString& RelativePath) const { return Inner.Exists(RelativePath); }
int64 FTerrainFaultDevice::Size(const FString& RelativePath) const   { return Inner.Size(RelativePath); }

ETerrainStorageResult FTerrainFaultDevice::Read(const FString& RelativePath, TArray<uint8>& OutBytes) const
{
	++Counts[static_cast<int32>(ETerrainStorageOp::Read)];
	return Inner.Read(RelativePath, OutBytes);
}

ETerrainStorageResult FTerrainFaultDevice::ListFiles(
	const FString& RelativeDirectory, TArray<FString>& OutNames) const
{
	// P-003 §8 names "segment discovery" among the mandatory injected failures, so listing is
	// faultable even though it mutates nothing. The const_cast is confined to the counter: a
	// read that can fail is still a read.
	int32 Tear = -1;
	if (const_cast<FTerrainFaultDevice*>(this)->ShouldFail(ETerrainStorageOp::ListFiles, RelativeDirectory, Tear))
	{
		return ETerrainStorageResult::IoError;
	}
	return Inner.ListFiles(RelativeDirectory, OutNames);
}

ETerrainStorageResult FTerrainFaultDevice::WriteNew(const FString& RelativePath, TArrayView<const uint8> Bytes)
{
	int32 Tear = -1;
	if (ShouldFail(ETerrainStorageOp::WriteNew, RelativePath, Tear))
	{
		if (Tear >= 0)
		{
			// A torn create: the name exists and the content is a prefix. This is the state a
			// crash between "extend the file" and "write all of it" leaves behind.
			const int32 Kept = FMath::Min(Tear, Bytes.Num());
			Inner.WriteNew(RelativePath, TArrayView<const uint8>(Bytes.GetData(), Kept));
		}
		return ETerrainStorageResult::IoError;
	}
	return Inner.WriteNew(RelativePath, Bytes);
}

ETerrainStorageResult FTerrainFaultDevice::OverwriteInPlace(const FString& RelativePath, TArrayView<const uint8> Bytes)
{
	int32 Tear = -1;
	if (ShouldFail(ETerrainStorageOp::OverwriteInPlace, RelativePath, Tear))
	{
		if (Tear >= 0)
		{
			// A torn slot overwrite: the first Tear bytes are new, the rest are still old. The
			// file keeps its length, which is what makes this different from a torn create and
			// what the whole-slot checksum is there to catch.
			TArray<uint8> Existing;
			if (Inner.Read(RelativePath, Existing) == ETerrainStorageResult::Ok
				&& Existing.Num() == Bytes.Num())
			{
				const int32 Kept = FMath::Min(Tear, Bytes.Num());
				FMemory::Memcpy(Existing.GetData(), Bytes.GetData(), Kept);
				Inner.OverwriteInPlace(RelativePath, Existing);
			}
		}
		return ETerrainStorageResult::IoError;
	}
	return Inner.OverwriteInPlace(RelativePath, Bytes);
}

ETerrainStorageResult FTerrainFaultDevice::Append(const FString& RelativePath, TArrayView<const uint8> Bytes)
{
	int32 Tear = -1;
	if (ShouldFail(ETerrainStorageOp::Append, RelativePath, Tear))
	{
		if (Tear >= 0)
		{
			const int32 Kept = FMath::Min(Tear, Bytes.Num());
			Inner.Append(RelativePath, TArrayView<const uint8>(Bytes.GetData(), Kept));
		}
		return ETerrainStorageResult::IoError;
	}
	return Inner.Append(RelativePath, Bytes);
}

ETerrainStorageResult FTerrainFaultDevice::Delete(const FString& RelativePath)
{
	int32 Tear = -1;
	if (ShouldFail(ETerrainStorageOp::Delete, RelativePath, Tear))
	{
		return ETerrainStorageResult::IoError;
	}
	return Inner.Delete(RelativePath);
}

// ---- the object store ---------------------------------------------------

ETerrainStorageResult FTerrainFileObjectStore::EnsureLayout()
{
	return Device.EnsureDirectory(TerrainStoragePaths::ObjectsDirectory);
}

bool FTerrainFileObjectStore::Contains(const FTerrainDigest& Digest) const
{
	return Device.Exists(TerrainStoragePaths::Object(Digest));
}

bool FTerrainFileObjectStore::LoadObject(const FTerrainDigest& Digest, TArray<uint8>& OutBytes) const
{
	TArray<uint8> Bytes;
	if (Device.Read(TerrainStoragePaths::Object(Digest), Bytes) != ETerrainStorageResult::Ok)
	{
		return false;
	}

	// Verified on the way out as well as on the way in. Content addressing checked only on
	// write is a naming convention; checked on read it is the reason a caller can trust the
	// bytes without trusting the medium.
	if (TerrainPersistDigest(Bytes) != Digest)
	{
		return false;
	}

	OutBytes = MoveTemp(Bytes);
	return true;
}

bool FTerrainFileObjectStore::StoreObject(const FTerrainDigest& Digest, TArrayView<const uint8> Bytes)
{
	if (TerrainPersistDigest(Bytes) != Digest)
	{
		return false;
	}

	const FString Path = TerrainStoragePaths::Object(Digest);
	if (Device.Exists(Path))
	{
		// Immutable and content-addressed: the same digest is the same bytes, so this is a
		// success that writes nothing rather than a conflict.
		return true;
	}

	if (Device.EnsureDirectory(TerrainStoragePaths::ObjectDirectory(Digest)) != ETerrainStorageResult::Ok)
	{
		return false;
	}

	const ETerrainStorageResult Result = Device.WriteNew(Path, Bytes);
	// A concurrent writer of the SAME object is not a failure, for the same reason as above.
	return Result == ETerrainStorageResult::Ok || Result == ETerrainStorageResult::AlreadyExists;
}

ETerrainStorageResult FTerrainFileObjectStore::DeleteObject(const FTerrainDigest& Digest)
{
	return Device.Delete(TerrainStoragePaths::Object(Digest));
}

// ---- the slot pair ------------------------------------------------------

ETerrainStorageResult FTerrainSlotPair::Create(
	const FTerrainPersistIdentity& Identity, TArrayView<const uint8> InitialBody)
{
	if (InitialBody.Num() != TerrainPersistSlotBodySize)
	{
		return ETerrainStorageResult::WrongSize;
	}

	TArray<uint8> Slot;
	if (!TerrainPersistEncodeSlot(Type, Identity, InitialBody, Slot))
	{
		return ETerrainStorageResult::WrongSize;
	}

	for (int32 Index = 0; Index < 2; ++Index)
	{
		const ETerrainStorageResult Result = Device.WriteNew(Paths[Index], Slot);
		if (Result != ETerrainStorageResult::Ok)
		{
			return Result;
		}
	}

	// Both slots now hold the same generation. Read() will pick slot 0 and Publish() will
	// therefore write slot 1 first, which is the alternation the protocol expects.
	NextPublishIndex = INDEX_NONE;
	bHasRead = false;
	return ETerrainStorageResult::Ok;
}

ETerrainStorageResult FTerrainSlotPair::Read(
	const FTerrainPersistIdentity& Identity,
	FTerrainSlotState& OutSlotZero,
	FTerrainSlotState& OutSlotOne,
	int32& OutBestIndex)
{
	FTerrainSlotState* States[2] = { &OutSlotZero, &OutSlotOne };
	OutBestIndex = INDEX_NONE;

	for (int32 Index = 0; Index < 2; ++Index)
	{
		FTerrainSlotState& State = *States[Index];
		State = FTerrainSlotState();

		TArray<uint8> Bytes;
		const ETerrainStorageResult ReadResult = Device.Read(Paths[Index], Bytes);
		if (ReadResult == ETerrainStorageResult::NotFound)
		{
			continue;
		}
		if (ReadResult != ETerrainStorageResult::Ok)
		{
			return ReadResult;
		}

		State.bPresent = true;

		if (Bytes.Num() != TerrainPersistSlotSize)
		{
			// A slot is a fixed-size file. Any other length means something other than this
			// protocol wrote it.
			State.Error = ETerrainPersistError::BodyLengthOutOfRange;
			continue;
		}

		FTerrainPersistObjectHeader Header;
		TArrayView<const uint8> Body;
		const ETerrainPersistError Error =
			TerrainPersistDecodeObject(Bytes, Type, &Identity, Header, Body);
		if (Error != ETerrainPersistError::None)
		{
			State.Error = Error;
			continue;
		}

		State.bValid = true;
		State.Body.Append(Body.GetData(), Body.Num());

		// The generation is the first eight bytes of every slot body -- Generation for a root
		// slot, AnchorGeneration for an anchor. That coincidence is deliberate and is what
		// lets this class stay ignorant of both bodies while still ordering them.
		FTerrainByteReader Reader(Body);
		State.Generation = Reader.ReadU64();
	}

	if (OutSlotZero.bValid && OutSlotOne.bValid)
	{
		OutBestIndex = (OutSlotOne.Generation > OutSlotZero.Generation) ? 1 : 0;
	}
	else if (OutSlotZero.bValid)
	{
		OutBestIndex = 0;
	}
	else if (OutSlotOne.bValid)
	{
		OutBestIndex = 1;
	}

	// Publish writes the slot that is NOT current, so a torn publication cannot damage the
	// state the store would otherwise fall back to. With neither valid there is nothing to
	// protect, so slot 0 is as good a choice as any.
	NextPublishIndex = (OutBestIndex == INDEX_NONE) ? 0 : (1 - OutBestIndex);
	bHasRead = true;
	return ETerrainStorageResult::Ok;
}

ETerrainStorageResult FTerrainSlotPair::Publish(
	const FTerrainPersistIdentity& Identity, TArrayView<const uint8> Body)
{
	if (!bHasRead || NextPublishIndex == INDEX_NONE)
	{
		// Publishing without knowing which slot is current risks overwriting the only good
		// copy. Refusing is the only safe answer; guessing is how a two-slot protocol becomes
		// a one-slot protocol at the worst possible moment.
		return ETerrainStorageResult::NotFound;
	}
	if (Body.Num() != TerrainPersistSlotBodySize)
	{
		return ETerrainStorageResult::WrongSize;
	}

	TArray<uint8> Slot;
	if (!TerrainPersistEncodeSlot(Type, Identity, Body, Slot))
	{
		return ETerrainStorageResult::WrongSize;
	}

	const ETerrainStorageResult Result = Device.OverwriteInPlace(Paths[NextPublishIndex], Slot);
	if (Result != ETerrainStorageResult::Ok)
	{
		// The slot we tried to write may now be torn. The OTHER slot is untouched and is still
		// the current state, so leave the alternation pointing here: the next attempt should
		// retry this slot, not start damaging the good one.
		return Result;
	}

	NextPublishIndex = 1 - NextPublishIndex;
	return ETerrainStorageResult::Ok;
}

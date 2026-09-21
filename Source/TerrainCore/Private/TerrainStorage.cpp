// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainStorage.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/Paths.h"
#include "TerrainCore.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#elif PLATFORM_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

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

	/**
	 * `<Prefix><16 lowercase hex><Suffix>` and nothing else.
	 *
	 * Shared by the segment and pack parsers because they are the same shape, and two copies
	 * of a name parser are two things that must agree with their writers and with each other,
	 * with no mechanism to.
	 */
	static bool ParseIdFileName(
		const FString& FileName, const TCHAR* Prefix, const TCHAR* Suffix, uint64& OutId)
	{
		const int32 PrefixLen = FCString::Strlen(Prefix);
		const int32 SuffixLen = FCString::Strlen(Suffix);

		if (!FileName.StartsWith(Prefix, ESearchCase::CaseSensitive)
			|| !FileName.EndsWith(Suffix, ESearchCase::CaseSensitive)
			|| FileName.Len() - PrefixLen - SuffixLen != 16)
		{
			return false;
		}

		uint64 Value = 0;
		for (int32 Index = 0; Index < 16; ++Index)
		{
			const TCHAR Char = FileName[PrefixLen + Index];
			uint64 Nibble;
			if      (Char >= TEXT('0') && Char <= TEXT('9')) { Nibble = static_cast<uint64>(Char - TEXT('0')); }
			else if (Char >= TEXT('a') && Char <= TEXT('f')) { Nibble = static_cast<uint64>(Char - TEXT('a') + 10); }
			else { return false; }   // lowercase only, matching what the writers emit
			Value = (Value << 4) | Nibble;
		}
		OutId = Value;
		return true;
	}

	bool ParseJournalSegment(const FString& FileName, uint64& OutSegmentId)
	{
		return ParseIdFileName(FileName, TEXT("seg-"), TEXT(".tjs"), OutSegmentId);
	}

	FString Pack(uint64 PackId)
	{
		return FString::Printf(TEXT("%s/pack-%016llx.tpk"), PacksDirectory, PackId);
	}

	bool ParsePack(const FString& FileName, uint64& OutPackId)
	{
		return ParseIdFileName(FileName, TEXT("pack-"), TEXT(".tpk"), OutPackId);
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

ETerrainStorageResult FTerrainPlatformStorageDevice::ReadRange(
	const FString& RelativePath, int64 Offset, int64 Length, TArray<uint8>& OutBytes) const
{
	const FString Absolute = Resolve(RelativePath);
	if (Absolute.IsEmpty())
	{
		return ETerrainStorageResult::BadPath;
	}
	if (Offset < 0 || Length < 0 || Length > MAX_int32)
	{
		return ETerrainStorageResult::WrongSize;
	}

	IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> Handle(File.OpenRead(*Absolute));
	if (!Handle.IsValid())
	{
		return File.FileExists(*Absolute) ? ETerrainStorageResult::IoError : ETerrainStorageResult::NotFound;
	}

	const int64 FileSize = Handle->Size();
	if (FileSize < 0)
	{
		return ETerrainStorageResult::IoError;
	}
	if (Offset > FileSize || Length > FileSize - Offset)
	{
		return ETerrainStorageResult::WrongSize;
	}

	OutBytes.Reset();
	OutBytes.AddUninitialized(static_cast<int32>(Length));
	if (Length > 0 && (!Handle->Seek(Offset) || !Handle->Read(OutBytes.GetData(), Length)))
	{
		OutBytes.Reset();
		return ETerrainStorageResult::IoError;
	}
	return ETerrainStorageResult::Ok;
}

ETerrainStorageResult FTerrainPlatformStorageDevice::Truncate(const FString& RelativePath, int64 NewSize)
{
	const FString Absolute = Resolve(RelativePath);
	if (Absolute.IsEmpty())
	{
		return ETerrainStorageResult::BadPath;
	}
	if (NewSize < 0)
	{
		return ETerrainStorageResult::WrongSize;
	}

	IPlatformFile& File = FPlatformFileManager::Get().GetPlatformFile();
	if (!File.FileExists(*Absolute))
	{
		return ETerrainStorageResult::NotFound;
	}

	// bAppend = true, for the same reason as OverwriteInPlace: bAppend = false truncates to zero
	// on open, which would turn "cut the torn tail" into "lose the container".
	TUniquePtr<IFileHandle> Handle(File.OpenWrite(*Absolute, /*bAppend=*/true, /*bAllowRead=*/true));
	if (!Handle.IsValid())
	{
		return ETerrainStorageResult::IoError;
	}

	const int64 Current = Handle->Size();
	if (Current < 0)
	{
		return ETerrainStorageResult::IoError;
	}
	if (NewSize > Current)
	{
		return ETerrainStorageResult::WrongSize;   // shrink only
	}
	if (NewSize < Current && !Handle->Truncate(NewSize))
	{
		return ETerrainStorageResult::IoError;
	}
	// The new length is metadata of this file, made durable by this file's own flush -- the same
	// primitive every journal append already relies on (P-005 §3).
	return Handle->Flush(/*bFullFlush=*/true) ? ETerrainStorageResult::Ok : ETerrainStorageResult::IoError;
}

ETerrainStorageResult FTerrainPlatformStorageDevice::SyncDirectory(const FString& RelativeDirectory)
{
	// The empty path names the world directory itself: base.tobj and the top-level directories
	// are entries in it, and it is the one directory no relative path can name.
	FString Absolute = RelativeDirectory.IsEmpty() ? Root : Resolve(RelativeDirectory);
	if (Absolute.IsEmpty())
	{
		return ETerrainStorageResult::BadPath;
	}
	Absolute = FPaths::ConvertRelativePathToFull(Absolute);

#if PLATFORM_WINDOWS
	// Win32 has no documented directory flush. A backup-semantics handle with write access is
	// the closest thing, and NTFS accepts FlushFileBuffers on it. Not being documented, a
	// failure here must not stop a world being created -- see the declaration.
	FPaths::MakePlatformFilename(Absolute);
	HANDLE Handle = ::CreateFileW(*Absolute, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (Handle == INVALID_HANDLE_VALUE)
	{
		UE_LOG(LogTerrainCore, Warning,
			TEXT("SyncDirectory: could not open '%s' (error %u); its entries are as durable as ")
			TEXT("NTFS makes them without help (P-005 §6)."), *Absolute, ::GetLastError());
		return ETerrainStorageResult::Ok;
	}
	const BOOL bFlushed = ::FlushFileBuffers(Handle);
	const uint32 FlushError = bFlushed ? 0 : ::GetLastError();
	::CloseHandle(Handle);
	if (!bFlushed)
	{
		UE_LOG(LogTerrainCore, Warning,
			TEXT("SyncDirectory: FlushFileBuffers on '%s' failed (error %u); continuing (P-005 §6)."),
			*Absolute, FlushError);
	}
	return ETerrainStorageResult::Ok;
#elif PLATFORM_UNIX
	// POSIX's documented primitive: fsync on the directory makes its entries durable. On the
	// shipping server this closes the bootstrap window rather than narrowing it.
	const int Descriptor = ::open(TCHAR_TO_UTF8(*Absolute), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (Descriptor < 0)
	{
		return ETerrainStorageResult::IoError;
	}
	const int Synced = ::fsync(Descriptor);
	::close(Descriptor);
	return Synced == 0 ? ETerrainStorageResult::Ok : ETerrainStorageResult::IoError;
#else
	return ETerrainStorageResult::Ok;
#endif
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

ETerrainStorageResult FTerrainMemoryStorageDevice::ReadRange(
	const FString& RelativePath, int64 Offset, int64 Length, TArray<uint8>& OutBytes) const
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
	if (Offset < 0 || Length < 0 || Offset > Found->Num() || Length > Found->Num() - Offset)
	{
		return ETerrainStorageResult::WrongSize;
	}
	OutBytes.Reset();
	OutBytes.Append(Found->GetData() + Offset, static_cast<int32>(Length));
	return ETerrainStorageResult::Ok;
}

ETerrainStorageResult FTerrainMemoryStorageDevice::Truncate(const FString& RelativePath, int64 NewSize)
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
	if (NewSize < 0 || NewSize > Found->Num())
	{
		return ETerrainStorageResult::WrongSize;
	}
	Found->SetNum(static_cast<int32>(NewSize));
	return ETerrainStorageResult::Ok;
}

ETerrainStorageResult FTerrainMemoryStorageDevice::SyncDirectory(const FString& RelativeDirectory)
{
	// Nothing in memory is more or less durable than anything else.
	return RelativeDirectory.IsEmpty() || TerrainStorageIsSafeRelativePath(RelativeDirectory)
		? ETerrainStorageResult::Ok : ETerrainStorageResult::BadPath;
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

void FTerrainFaultDevice::FailAtMutation(int32 Index, int32 TearBytes)
{
	MutationFaultIndex = Index;
	MutationTearBytes  = TearBytes;
}

bool FTerrainFaultDevice::ShouldFail(ETerrainStorageOp Op, const FString& Path, int32& OutTearBytes)
{
	++Counts[static_cast<int32>(Op)];

	// The session's mutating writes form one ordered sequence, and a crash happens at one point
	// in it. Counted before the per-op faults so the two are independent.
	const bool bMutating = Op == ETerrainStorageOp::WriteNew
		|| Op == ETerrainStorageOp::OverwriteInPlace
		|| Op == ETerrainStorageOp::Append
		|| Op == ETerrainStorageOp::Delete
		|| Op == ETerrainStorageOp::Truncate;
	if (bMutating)
	{
		const int32 ThisMutation = Mutations++;
		if (MutationFaultIndex >= 0 && ThisMutation == MutationFaultIndex)
		{
			OutTearBytes = MutationTearBytes;
			MutationFaultIndex = -1;   // one-shot, like the per-op faults
			return true;
		}
	}

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

ETerrainStorageResult FTerrainFaultDevice::ReadRange(
	const FString& RelativePath, int64 Offset, int64 Length, TArray<uint8>& OutBytes) const
{
	++Counts[static_cast<int32>(ETerrainStorageOp::Read)];
	return Inner.ReadRange(RelativePath, Offset, Length, OutBytes);
}

ETerrainStorageResult FTerrainFaultDevice::Truncate(const FString& RelativePath, int64 NewSize)
{
	int32 Tear = -1;
	if (ShouldFail(ETerrainStorageOp::Truncate, RelativePath, Tear))
	{
		// A file length is set or it is not; there is no half-truncation to model, so a failed
		// truncation changes nothing and the torn mode is the same as the hard one.
		return ETerrainStorageResult::IoError;
	}
	return Inner.Truncate(RelativePath, NewSize);
}

ETerrainStorageResult FTerrainFaultDevice::SyncDirectory(const FString& RelativeDirectory)
{
	int32 Tear = -1;
	if (ShouldFail(ETerrainStorageOp::SyncDirectory, RelativeDirectory, Tear))
	{
		return ETerrainStorageResult::IoError;
	}
	return Inner.SyncDirectory(RelativeDirectory);
}

// ---- the object store ---------------------------------------------------

namespace
{
	void PutU32(TArray<uint8>& Out, uint32 Value)
	{
		for (int32 i = 0; i < 4; ++i) { Out.Add(static_cast<uint8>((Value >> (i * 8)) & 0xFFu)); }
	}

	void PutU64(TArray<uint8>& Out, uint64 Value)
	{
		for (int32 i = 0; i < 8; ++i) { Out.Add(static_cast<uint8>((Value >> (i * 8)) & 0xFFu)); }
	}

	uint32 GetU32(const uint8* P)
	{
		uint32 V = 0;
		for (int32 i = 0; i < 4; ++i) { V |= static_cast<uint32>(P[i]) << (i * 8); }
		return V;
	}

	uint64 GetU64(const uint8* P)
	{
		uint64 V = 0;
		for (int32 i = 0; i < 8; ++i) { V |= static_cast<uint64>(P[i]) << (i * 8); }
		return V;
	}

	/** Byte order, so every list this store writes or migrates is in a deterministic order. */
	bool DigestLess(const FTerrainDigest& A, const FTerrainDigest& B)
	{
		return FMemory::Memcmp(A.Bytes, B.Bytes, TerrainPersistDigestSize) < 0;
	}
}

namespace TerrainStoragePaths
{
	FString Container(int32 ContainerIndex)
	{
		check(ContainerIndex >= 0 && ContainerIndex < ContainerCount);
		return FString::Printf(TEXT("%s/c.%d"), ContainersDirectory, ContainerIndex);
	}
}

ETerrainStorageResult FTerrainFileObjectStore::EnsureLayout(bool* OutCreated)
{
	if (OutCreated != nullptr) { *OutCreated = false; }

	const ETerrainStorageResult DirectoryResult =
		Device.EnsureDirectory(TerrainStoragePaths::ContainersDirectory);
	if (DirectoryResult != ETerrainStorageResult::Ok)
	{
		return DirectoryResult;
	}

	for (int32 Index = 0; Index < TerrainStoragePaths::ContainerCount; ++Index)
	{
		const FString Path = TerrainStoragePaths::Container(Index);
		if (Device.Exists(Path))
		{
			continue;
		}
		// Bootstrap, and the only name this store ever creates (P-005 §6). An empty container
		// is a zero-length file: frames start at offset 0 and there is no header to tear.
		const ETerrainStorageResult Created = Device.WriteNew(Path, TArrayView<const uint8>());
		if (Created != ETerrainStorageResult::Ok && Created != ETerrainStorageResult::AlreadyExists)
		{
			return Created;
		}
		if (OutCreated != nullptr) { *OutCreated = true; }
	}
	return ETerrainStorageResult::Ok;
}

int32 FTerrainFileObjectStore::ResolveContainer(const FTerrainDigest& Digest) const
{
	for (int32 Index = 0; Index < TerrainStoragePaths::ContainerCount; ++Index)
	{
		if (Containers[Index].Index.Contains(Digest))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool FTerrainFileObjectStore::Contains(const FTerrainDigest& Digest) const
{
	return BatchEntries.Contains(Digest)
		|| ResolveContainer(Digest) != INDEX_NONE
		|| LegacyPacks.Contains(Digest)
		|| Device.Exists(TerrainStoragePaths::Object(Digest));
}

bool FTerrainFileObjectStore::LoadFrom(const FString& Path, const FObjectLocation& Where,
	const FTerrainDigest& Digest, TArray<uint8>& OutBytes) const
{
	TArray<uint8> Bytes;
	if (Device.ReadRange(Path, Where.Offset, Where.Length, Bytes) != ETerrainStorageResult::Ok)
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

bool FTerrainFileObjectStore::LoadObject(const FTerrainDigest& Digest, TArray<uint8>& OutBytes) const
{
	// An object written earlier in this batch must read back NOW, before it is durable. The
	// index path-copy reads pages it wrote moments ago in the same capture.
	if (const FObjectLocation* Buffered = BatchEntries.Find(Digest))
	{
		TArray<uint8> Bytes;
		Bytes.Append(BatchBuffer.GetData() + Buffered->Offset, Buffered->Length);
		if (TerrainPersistDigest(Bytes) != Digest)
		{
			return false;
		}
		OutBytes = MoveTemp(Bytes);
		return true;
	}

	// Every copy, in resolution order; the first that reads and verifies wins. Normally there is
	// one. There are two in the window between a compaction's copy and its truncation, and if
	// that truncation's outcome is uncertain the copy that still verifies is the right answer.
	for (int32 Index = 0; Index < TerrainStoragePaths::ContainerCount; ++Index)
	{
		if (const FObjectLocation* Where = Containers[Index].Index.Find(Digest))
		{
			if (LoadFrom(TerrainStoragePaths::Container(Index), *Where, Digest, OutBytes))
			{
				return true;
			}
		}
	}

	if (const TPair<uint64, FObjectLocation>* Legacy = LegacyPacks.Find(Digest))
	{
		if (LoadFrom(TerrainStoragePaths::Pack(Legacy->Key), Legacy->Value, Digest, OutBytes))
		{
			return true;
		}
	}

	TArray<uint8> Loose;
	if (Device.Read(TerrainStoragePaths::Object(Digest), Loose) == ETerrainStorageResult::Ok
		&& TerrainPersistDigest(Loose) == Digest)
	{
		OutBytes = MoveTemp(Loose);
		return true;
	}
	return false;
}

bool FTerrainFileObjectStore::StoreObject(const FTerrainDigest& Digest, TArrayView<const uint8> Bytes)
{
	if (TerrainPersistDigest(Bytes) != Digest)
	{
		return false;
	}

	if (Contains(Digest))
	{
		// Immutable and content-addressed: already held, in this batch, a container or a
		// pre-P-005 file. The same digest is the same bytes, so this writes nothing.
		return true;
	}

	// Outside a batch, one object is one frame. This used to create a loose file, which was
	// a new name at runtime -- the thing P-005 removes.
	const bool bImplicitBatch = !bBatchOpen;
	if (bImplicitBatch)
	{
		BeginBatch();
	}

	FObjectLocation Where;
	Where.Offset = BatchBuffer.Num();
	Where.Length = Bytes.Num();
	BatchBuffer.Append(Bytes.GetData(), Bytes.Num());
	BatchEntries.Add(Digest, Where);
	BatchOrder.Add(Digest);

	return bImplicitBatch ? CommitBatch() == ETerrainStorageResult::Ok : true;
}

// ---- batching (P-004 §13, P-005 §4) -------------------------------------

void FTerrainFileObjectStore::BeginBatch()
{
	checkf(!bBatchOpen, TEXT("A pack batch is already open on this store."));
	bBatchOpen = true;
	BatchBuffer.Reset();
	BatchEntries.Reset();
	BatchOrder.Reset();
}

void FTerrainFileObjectStore::AbandonBatch()
{
	bBatchOpen = false;
	BatchBuffer.Reset();
	BatchEntries.Reset();
	BatchOrder.Reset();
}

void FTerrainFileObjectStore::BuildBatchImage(TArray<uint8>& OutImage) const
{
	// Body is already assembled in BatchBuffer; append the manifest, then the trailer. The
	// manifest follows store order, so the same captures always produce the same bytes.
	const int64 ManifestOffset = BatchBuffer.Num();
	OutImage.Reset();
	OutImage.Reserve(ManifestOffset + BatchOrder.Num() * TerrainPackEntrySize + TerrainPackTrailerSize);
	OutImage.Append(BatchBuffer);

	for (const FTerrainDigest& Digest : BatchOrder)
	{
		const FObjectLocation& Where = BatchEntries.FindChecked(Digest);
		OutImage.Append(Digest.Bytes, TerrainPersistDigestSize);
		PutU64(OutImage, static_cast<uint64>(Where.Offset));
		PutU32(OutImage, static_cast<uint32>(Where.Length));
	}

	// The checksum covers body and manifest, so a torn image fails here rather than being
	// half believed.
	const uint64 Checksum = TerrainPersistChecksum(OutImage);
	PutU64(OutImage, TerrainPackMagic);
	PutU32(OutImage, TerrainPackVersion);
	PutU32(OutImage, static_cast<uint32>(BatchOrder.Num()));
	PutU64(OutImage, static_cast<uint64>(ManifestOffset));
	PutU64(OutImage, Checksum);
}

ETerrainStorageResult FTerrainFileObjectStore::CommitBatch()
{
	if (!bBatchOpen)
	{
		return ETerrainStorageResult::Ok;
	}
	if (BatchEntries.Num() == 0)
	{
		AbandonBatch();
		return ETerrainStorageResult::Ok;
	}
	if (BatchEntries.Num() > TerrainPackMaxEntries)
	{
		AbandonBatch();
		return ETerrainStorageResult::IoError;
	}

	FContainerState& Target = Containers[ActiveContainer];
	const FString Path = TerrainStoragePaths::Container(ActiveContainer);

	// --- P-005 §4.3: append only at the valid end -----------------------------------------------
	const int64 OnDisk = Device.Size(Path);
	if (OnDisk < 0)
	{
		// The pool is missing. Creating it here would be a runtime name, which is the one thing
		// this design forbids; the world was not bootstrapped, so refuse.
		AbandonBatch();
		return ETerrainStorageResult::NotFound;
	}
	if (OnDisk < Target.ValidEnd)
	{
		// Shorter than the frames we indexed: something outside this store cut it, and the
		// index can no longer be trusted to describe it.
		AbandonBatch();
		return ETerrainStorageResult::IoError;
	}
	if (OnDisk > Target.ValidEnd)
	{
		// A torn tail -- from a previous process, or a failed append earlier in this one. A
		// frame written after it would be durable, named by a root, and unreachable by the scan.
		const ETerrainStorageResult Cut = Device.Truncate(Path, Target.ValidEnd);
		if (Cut != ETerrainStorageResult::Ok)
		{
			AbandonBatch();
			return Cut;
		}
	}

	TArray<uint8> Image;
	BuildBatchImage(Image);
	if (Image.Num() > TerrainFrameMaxBody)
	{
		AbandonBatch();
		return ETerrainStorageResult::IoError;
	}

	TArray<uint8> Frame;
	Frame.Reserve(TerrainFrameHeaderSize + Image.Num());
	PutU64(Frame, TerrainFrameMagic);
	PutU64(Frame, static_cast<uint64>(Target.ValidEnd));   // where this frame is, and nowhere else
	PutU64(Frame, static_cast<uint64>(Image.Num()));
	PutU32(Frame, TerrainFrameVersion);
	PutU32(Frame, 0);
	PutU64(Frame, TerrainPersistChecksum(TArrayView<const uint8>(Frame.GetData(), 32)));
	Frame.Append(Image);

	// One append, one flush. This is the whole point of the batch.
	const ETerrainStorageResult Result = Device.Append(Path, Frame);
	if (Result != ETerrainStorageResult::Ok)
	{
		// Whatever reached the file is past ValidEnd, and the next append cuts it.
		AbandonBatch();
		return Result;
	}

	// Only once it is durable do the buffered objects become resolvable from the container.
	const int64 BodyStart = Target.ValidEnd + TerrainFrameHeaderSize;
	for (const FTerrainDigest& Digest : BatchOrder)
	{
		FObjectLocation Where = BatchEntries.FindChecked(Digest);
		Where.Offset += BodyStart;
		Target.Index.FindOrAdd(Digest, Where);
		Target.ObjectBytes += Where.Length;
	}
	Target.ValidEnd += Frame.Num();

	AbandonBatch();
	return ETerrainStorageResult::Ok;
}

// ---- reading what is on disk --------------------------------------------

bool FTerrainFileObjectStore::ParsePackImage(TArrayView<const uint8> Image,
	TArray<TPair<FTerrainDigest, FObjectLocation>>& OutEntries)
{
	OutEntries.Reset();

	// P-004 §13.4, rules 1-5: any failure condemns the whole image.
	if (Image.Num() < TerrainPackTrailerSize)
	{
		return false;
	}
	const uint8* const Trailer = Image.GetData() + Image.Num() - TerrainPackTrailerSize;
	if (GetU64(Trailer) != TerrainPackMagic || GetU32(Trailer + 8) != TerrainPackVersion)
	{
		return false;
	}
	const uint32 Count          = GetU32(Trailer + 12);
	const uint64 ManifestOffset = GetU64(Trailer + 16);
	const uint64 Checksum       = GetU64(Trailer + 24);
	if (Count > static_cast<uint32>(TerrainPackMaxEntries))
	{
		return false;
	}
	const uint64 ManifestBytes = static_cast<uint64>(Count) * TerrainPackEntrySize;
	if (ManifestOffset > static_cast<uint64>(Image.Num())
		|| ManifestOffset + ManifestBytes + TerrainPackTrailerSize != static_cast<uint64>(Image.Num()))
	{
		return false;
	}
	if (TerrainPersistChecksum(Image.Slice(0, Image.Num() - TerrainPackTrailerSize)) != Checksum)
	{
		return false;
	}

	const uint8* Entry = Image.GetData() + ManifestOffset;
	for (uint32 i = 0; i < Count; ++i, Entry += TerrainPackEntrySize)
	{
		FTerrainDigest Digest;
		FMemory::Memcpy(Digest.Bytes, Entry, TerrainPersistDigestSize);
		const uint64 Offset = GetU64(Entry + TerrainPersistDigestSize);
		const uint32 Length = GetU32(Entry + TerrainPersistDigestSize + 8);

		// Rule 6: an entry pointing outside the body is skipped on its own.
		if (Offset > ManifestOffset || Length > ManifestOffset - Offset || Length > MAX_int32)
		{
			continue;
		}
		FObjectLocation Where;
		Where.Offset = static_cast<int64>(Offset);
		Where.Length = static_cast<int32>(Length);
		OutEntries.Emplace(Digest, Where);
	}
	return true;
}

ETerrainStorageResult FTerrainFileObjectStore::ScanContainer(int32 ContainerIndex)
{
	FContainerState& State = Containers[ContainerIndex];
	State = FContainerState();

	const FString Path = TerrainStoragePaths::Container(ContainerIndex);
	const int64 Size = Device.Size(Path);
	if (Size <= 0)
	{
		return ETerrainStorageResult::Ok;   // empty, or absent until EnsureLayout runs
	}

	// P-005 §4.2: walk frames from offset 0; the first whose HEADER fails ends the scan, and
	// where it starts is the valid end. Nothing past it was ever named by a root.
	int64 Position = 0;
	while (Size - Position >= TerrainFrameHeaderSize)
	{
		TArray<uint8> Header;
		const ETerrainStorageResult HeaderRead =
			Device.ReadRange(Path, Position, TerrainFrameHeaderSize, Header);
		if (HeaderRead != ETerrainStorageResult::Ok)
		{
			return HeaderRead;   // a device failure is not a torn frame
		}

		const uint8* H = Header.GetData();
		if (GetU64(H) != TerrainFrameMagic
			|| GetU64(H + 8) != static_cast<uint64>(Position)
			|| GetU32(H + 24) != TerrainFrameVersion
			|| GetU32(H + 28) != 0
			|| GetU64(H + 32) != TerrainPersistChecksum(TArrayView<const uint8>(H, 32)))
		{
			break;
		}
		const uint64 BodyLength = GetU64(H + 16);
		const int64 Remaining = Size - Position - TerrainFrameHeaderSize;
		if (BodyLength < static_cast<uint64>(TerrainPackTrailerSize)
			|| BodyLength > static_cast<uint64>(TerrainFrameMaxBody)
			|| BodyLength > static_cast<uint64>(Remaining))
		{
			break;
		}

		const int64 BodyStart = Position + TerrainFrameHeaderSize;
		TArray<uint8> Body;
		const ETerrainStorageResult BodyRead =
			Device.ReadRange(Path, BodyStart, static_cast<int64>(BodyLength), Body);
		if (BodyRead != ETerrainStorageResult::Ok)
		{
			return BodyRead;
		}

		// A whole frame whose header holds but whose body does not is skipped, not a stop: its
		// length is trustworthy, so the frames after it still are. That is bit rot, or a flush
		// that extended the file without its data -- either way nothing it held is believed.
		TArray<TPair<FTerrainDigest, FObjectLocation>> Entries;
		if (!ParsePackImage(Body, Entries))
		{
			UE_LOG(LogTerrainCore, Warning,
				TEXT("Container %d: the frame at %lld fails its body checks and is ignored."),
				ContainerIndex, Position);
		}
		for (TPair<FTerrainDigest, FObjectLocation>& Entry : Entries)
		{
			Entry.Value.Offset += BodyStart;
			State.Index.FindOrAdd(Entry.Key, Entry.Value);
			State.ObjectBytes += Entry.Value.Length;
		}
		Position = BodyStart + static_cast<int64>(BodyLength);
	}

	State.ValidEnd = Position;
	if (Position != Size)
	{
		UE_LOG(LogTerrainCore, Log,
			TEXT("Container %d: %lld bytes past the last whole frame (a torn append); the next ")
			TEXT("append cuts them."), ContainerIndex, Size - Position);
	}
	return ETerrainStorageResult::Ok;
}

ETerrainStorageResult FTerrainFileObjectStore::LoadLegacyPacks()
{
	LegacyPacks.Reset();

	TArray<uint64> PackIds;
	const ETerrainStorageResult Listed = ListPacks(PackIds);
	if (Listed != ETerrainStorageResult::Ok)
	{
		return Listed;
	}

	for (const uint64 PackId : PackIds)
	{
		TArray<uint8> Image;
		if (Device.Read(TerrainStoragePaths::Pack(PackId), Image) != ETerrainStorageResult::Ok)
		{
			continue;
		}
		TArray<TPair<FTerrainDigest, FObjectLocation>> Entries;
		if (!ParsePackImage(Image, Entries))
		{
			continue;   // torn or corrupt: unreferenced by construction, so ignored
		}
		for (const TPair<FTerrainDigest, FObjectLocation>& Entry : Entries)
		{
			// Earlier packs win, as they always did.
			LegacyPacks.FindOrAdd(Entry.Key, TPair<uint64, FObjectLocation>(PackId, Entry.Value));
		}
	}
	return ETerrainStorageResult::Ok;
}

ETerrainStorageResult FTerrainFileObjectStore::LoadPacks()
{
	for (int32 Index = 0; Index < TerrainStoragePaths::ContainerCount; ++Index)
	{
		const ETerrainStorageResult Scanned = ScanContainer(Index);
		if (Scanned != ETerrainStorageResult::Ok)
		{
			return Scanned;
		}
	}

	const ETerrainStorageResult Legacy = LoadLegacyPacks();
	if (Legacy != ETerrainStorageResult::Ok)
	{
		return Legacy;
	}

	// P-005 §4.5: keep writing where the world was last written. Policy, not correctness.
	ActiveContainer = 0;
	int64 Largest = 0;
	for (int32 Index = 0; Index < TerrainStoragePaths::ContainerCount; ++Index)
	{
		if (Containers[Index].ValidEnd > Largest)
		{
			Largest = Containers[Index].ValidEnd;
			ActiveContainer = Index;
		}
	}
	return ETerrainStorageResult::Ok;
}

int32 FTerrainFileObjectStore::NumPackedObjects() const
{
	TSet<FTerrainDigest> Distinct;
	for (const FContainerState& State : Containers)
	{
		for (const TPair<FTerrainDigest, FObjectLocation>& Entry : State.Index)
		{
			Distinct.Add(Entry.Key);
		}
	}
	for (const TPair<FTerrainDigest, TPair<uint64, FObjectLocation>>& Entry : LegacyPacks)
	{
		Distinct.Add(Entry.Key);
	}
	return Distinct.Num();
}

// ---- containers ---------------------------------------------------------

int64 FTerrainFileObjectStore::ContainerValidEnd(int32 ContainerIndex) const
{
	return ContainerIndex >= 0 && ContainerIndex < TerrainStoragePaths::ContainerCount
		? Containers[ContainerIndex].ValidEnd : -1;
}

void FTerrainFileObjectStore::GetContainerContents(int32 ContainerIndex, TArray<FTerrainDigest>& OutDigests) const
{
	OutDigests.Reset();
	if (ContainerIndex < 0 || ContainerIndex >= TerrainStoragePaths::ContainerCount)
	{
		return;
	}
	for (const TPair<FTerrainDigest, FObjectLocation>& Entry : Containers[ContainerIndex].Index)
	{
		if (ResolveContainer(Entry.Key) == ContainerIndex)
		{
			OutDigests.Add(Entry.Key);
		}
	}
}

int64 FTerrainFileObjectStore::ContainerDeadBytes(int32 ContainerIndex, const TSet<FTerrainDigest>& Live) const
{
	if (ContainerIndex < 0 || ContainerIndex >= TerrainStoragePaths::ContainerCount)
	{
		return 0;
	}
	const FContainerState& State = Containers[ContainerIndex];
	int64 LiveBytes = 0;
	for (const TPair<FTerrainDigest, FObjectLocation>& Entry : State.Index)
	{
		if (Live.Contains(Entry.Key) && ResolveContainer(Entry.Key) == ContainerIndex)
		{
			LiveBytes += Entry.Value.Length;
		}
	}
	return State.ObjectBytes - LiveBytes;
}

bool FTerrainFileObjectStore::RotateActiveToEmpty()
{
	if (bBatchOpen || Containers[ActiveContainer].ValidEnd == 0)
	{
		return false;
	}
	for (int32 Index = 0; Index < TerrainStoragePaths::ContainerCount; ++Index)
	{
		if (Index != ActiveContainer && Containers[Index].ValidEnd == 0
			&& Device.Size(TerrainStoragePaths::Container(Index)) >= 0)
		{
			ActiveContainer = Index;
			return true;
		}
	}
	return false;
}

ETerrainStorageResult FTerrainFileObjectStore::CompactContainer(
	int32 ContainerIndex, const TSet<FTerrainDigest>& Live, int32& OutKept, int32& OutDropped)
{
	OutKept = 0;
	OutDropped = 0;

	if (ContainerIndex < 0 || ContainerIndex >= TerrainStoragePaths::ContainerCount)
	{
		return ETerrainStorageResult::BadPath;
	}
	if (bBatchOpen || ContainerIndex == ActiveContainer)
	{
		// Compacting the active container would copy it into itself and then cut itself.
		return ETerrainStorageResult::IoError;
	}

	FContainerState& Source = Containers[ContainerIndex];
	if (Source.ValidEnd == 0)
	{
		return ETerrainStorageResult::Ok;
	}
	const FString SourcePath = TerrainStoragePaths::Container(ContainerIndex);

	// What survives: live digests whose resolving copy is here. Everything else in the index --
	// dead objects, and copies shadowed by an earlier container -- is dropped.
	TArray<FTerrainDigest> Survivors;
	for (const TPair<FTerrainDigest, FObjectLocation>& Entry : Source.Index)
	{
		if (Live.Contains(Entry.Key) && ResolveContainer(Entry.Key) == ContainerIndex)
		{
			Survivors.Add(Entry.Key);
		}
		else
		{
			++OutDropped;
		}
	}
	// In container order, so a compacted frame keeps its objects in the order they were written.
	Survivors.Sort([&Source](const FTerrainDigest& A, const FTerrainDigest& B)
	{
		return Source.Index.FindChecked(A).Offset < Source.Index.FindChecked(B).Offset;
	});
	OutKept = Survivors.Num();

	const int32 TargetIndex = ActiveContainer;
	const FString TargetPath = TerrainStoragePaths::Container(TargetIndex);

	// --- 1. copy the survivors into one frame in the active container ----------------------------
	BeginBatch();
	for (const FTerrainDigest& Digest : Survivors)
	{
		if (Containers[TargetIndex].Index.Contains(Digest))
		{
			continue;   // the active container already holds a copy
		}
		TArray<uint8> Bytes;
		if (!LoadFrom(SourcePath, Source.Index.FindChecked(Digest), Digest, Bytes))
		{
			AbandonBatch();
			return ETerrainStorageResult::IoError;   // nothing written, nothing cut
		}
		FObjectLocation Where;
		Where.Offset = BatchBuffer.Num();
		Where.Length = Bytes.Num();
		BatchBuffer.Append(Bytes);
		BatchEntries.Add(Digest, Where);
		BatchOrder.Add(Digest);
	}

	// Durable first. Until this returns Ok the source is the only copy, and it is untouched.
	const ETerrainStorageResult Written = CommitBatch();
	if (Written != ETerrainStorageResult::Ok)
	{
		return Written;
	}

	// --- 2. read every survivor back from the ACTIVE container specifically ----------------------
	// LoadObject would be satisfied by the source copy, which is exactly the copy about to go.
	for (const FTerrainDigest& Digest : Survivors)
	{
		const FObjectLocation* Copy = Containers[TargetIndex].Index.Find(Digest);
		TArray<uint8> Verified;
		if (Copy == nullptr || !LoadFrom(TargetPath, *Copy, Digest, Verified))
		{
			return ETerrainStorageResult::IoError;
		}
	}

	// --- 3. only now cut the source --------------------------------------------------------------
	// No name is created or removed. The copy was flushed before this call was issued, so a
	// power cut can at most undo the truncation, leaving a duplicate (P-005 §5).
	const ETerrainStorageResult Cut = Device.Truncate(SourcePath, 0);
	if (Cut != ETerrainStorageResult::Ok)
	{
		// The file may or may not have been cut. Describe what is actually there rather than
		// what we hoped; every survivor has a verified copy in the active container either way.
		ScanContainer(ContainerIndex);
		return Cut;
	}
	Source = FContainerState();
	return ETerrainStorageResult::Ok;
}

// ---- pre-P-005 files ----------------------------------------------------

ETerrainStorageResult FTerrainFileObjectStore::ListLooseObjects(TArray<FTerrainDigest>& OutDigests) const
{
	OutDigests.Reset();

	static const FString Suffix = TEXT(".tobj");
	for (int32 Prefix = 0; Prefix < 256; ++Prefix)
	{
		const FString Directory = FString::Printf(
			TEXT("%s/%02x"), TerrainStoragePaths::ObjectsDirectory, Prefix);

		TArray<FString> Names;
		const ETerrainStorageResult Listed = Device.ListFiles(Directory, Names);
		if (Listed == ETerrainStorageResult::NotFound)
		{
			continue;   // a fan-out directory that was never created
		}
		if (Listed != ETerrainStorageResult::Ok)
		{
			return Listed;
		}

		for (const FString& Name : Names)
		{
			if (!Name.EndsWith(Suffix, ESearchCase::CaseSensitive))
			{
				continue;
			}
			FTerrainDigest Digest;
			if (TerrainPersistDigestFromHex(Name.LeftChop(Suffix.Len()), Digest))
			{
				OutDigests.Add(Digest);
			}
			// A file whose name is not a digest is left alone. Reclamation deleting something
			// it could not name would be deleting something it does not understand.
		}
	}
	return ETerrainStorageResult::Ok;
}

ETerrainStorageResult FTerrainFileObjectStore::ListPacks(TArray<uint64>& OutPackIds) const
{
	OutPackIds.Reset();

	TArray<FString> Names;
	const ETerrainStorageResult Listed =
		Device.ListFiles(TerrainStoragePaths::PacksDirectory, Names);
	if (Listed == ETerrainStorageResult::NotFound)
	{
		return ETerrainStorageResult::Ok;
	}
	if (Listed != ETerrainStorageResult::Ok)
	{
		return Listed;
	}

	for (const FString& Name : Names)
	{
		uint64 PackId = 0;
		if (TerrainStoragePaths::ParsePack(Name, PackId))
		{
			OutPackIds.Add(PackId);
		}
	}
	OutPackIds.Sort();
	return ETerrainStorageResult::Ok;
}

ETerrainStorageResult FTerrainFileObjectStore::MigrateLegacy(const TSet<FTerrainDigest>& Live,
	int32& OutMigrated, int32& OutFilesDeleted, int64& OutBytesDeleted)
{
	OutMigrated = 0;
	OutFilesDeleted = 0;
	OutBytesDeleted = 0;

	if (bBatchOpen)
	{
		return ETerrainStorageResult::IoError;
	}

	TArray<FTerrainDigest> Loose;
	const ETerrainStorageResult LooseListed = ListLooseObjects(Loose);
	if (LooseListed != ETerrainStorageResult::Ok)
	{
		return LooseListed;
	}
	TArray<uint64> PackIds;
	const ETerrainStorageResult PacksListed = ListPacks(PackIds);
	if (PacksListed != ETerrainStorageResult::Ok)
	{
		return PacksListed;
	}
	if (Loose.Num() == 0 && PackIds.Num() == 0)
	{
		return ETerrainStorageResult::Ok;
	}

	// --- 1. every live object that no container holds yet, into one frame ------------------------
	TArray<FTerrainDigest> ToCopy;
	for (const FTerrainDigest& Digest : Live)
	{
		if (ResolveContainer(Digest) == INDEX_NONE)
		{
			ToCopy.Add(Digest);
		}
	}
	ToCopy.Sort([](const FTerrainDigest& A, const FTerrainDigest& B) { return DigestLess(A, B); });

	BeginBatch();
	for (const FTerrainDigest& Digest : ToCopy)
	{
		TArray<uint8> Bytes;
		if (!LoadObject(Digest, Bytes))   // resolves through the pre-P-005 files, verified
		{
			AbandonBatch();
			return ETerrainStorageResult::IoError;
		}
		FObjectLocation Where;
		Where.Offset = BatchBuffer.Num();
		Where.Length = Bytes.Num();
		BatchBuffer.Append(Bytes);
		BatchEntries.Add(Digest, Where);
		BatchOrder.Add(Digest);
	}
	const ETerrainStorageResult Written = CommitBatch();
	if (Written != ETerrainStorageResult::Ok)
	{
		return Written;   // no pre-P-005 file has been touched
	}

	for (const FTerrainDigest& Digest : ToCopy)
	{
		const int32 Where = ResolveContainer(Digest);
		TArray<uint8> Verified;
		if (Where == INDEX_NONE
			|| !LoadFrom(TerrainStoragePaths::Container(Where),
			             Containers[Where].Index.FindChecked(Digest), Digest, Verified))
		{
			return ETerrainStorageResult::IoError;
		}
	}
	OutMigrated = ToCopy.Num();

	// --- 2. only now remove the old names -----------------------------------------------------------
	// A lost removal brings back a byte-identical duplicate; nothing is lost either way.
	auto Remove = [this, &OutFilesDeleted, &OutBytesDeleted](const FString& Path) -> ETerrainStorageResult
	{
		const int64 Size = Device.Size(Path);
		const ETerrainStorageResult Deleted = Device.Delete(Path);
		if (Deleted != ETerrainStorageResult::Ok && Deleted != ETerrainStorageResult::NotFound)
		{
			return Deleted;
		}
		++OutFilesDeleted;
		OutBytesDeleted += FMath::Max<int64>(Size, 0);
		return ETerrainStorageResult::Ok;
	};

	for (const FTerrainDigest& Digest : Loose)
	{
		const ETerrainStorageResult Removed = Remove(TerrainStoragePaths::Object(Digest));
		if (Removed != ETerrainStorageResult::Ok) { return Removed; }
	}
	for (const uint64 PackId : PackIds)
	{
		const ETerrainStorageResult Removed = Remove(TerrainStoragePaths::Pack(PackId));
		if (Removed != ETerrainStorageResult::Ok) { return Removed; }
	}
	LegacyPacks.Reset();
	return ETerrainStorageResult::Ok;
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

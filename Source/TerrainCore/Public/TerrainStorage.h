// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"
#include "TerrainPersistenceFormat.h"
#include "TerrainPersistenceIndex.h"

/**
 * TerrainStorage.h -- the storage device seam and the two stores built on it (P-004 §11, §12).
 *
 * P-003 §8 item 3 asks for the storage owner. This header is its bottom half: the small file
 * primitive every durable object goes through, and the two things schema 2 actually stores --
 * content-addressed immutable objects, and pre-created fixed-size slot pairs.
 *
 * WHY A SEAM AND NOT DIRECT IPlatformFile CALLS. P-003 requires a crash matrix with faults
 * injected "before/after apply, append/flush, SQLite begin/write/commit, broadcast/receipt,
 * every payload/page/descriptor write/flush, root overwrite/flush, segment discovery/rotation
 * and every deletion". You cannot inject a fault into a direct call. Every durable write in
 * this project goes through ITerrainStorageDevice so that FTerrainFaultDevice can make any one
 * of them fail, or tear, on demand -- and so the same tests run headless with no file system.
 *
 * WHAT THIS IS NOT. There is no journal writer, no checkpoint publication, no recovery and no
 * world store here. Those sit above this seam and are the next increments.
 */

// ---- results ------------------------------------------------------------

/**
 * Deliberately separate from ETerrainPersistError. That enum is about what a *byte* means;
 * this one is about what a *device* did. A caller that conflates "this file is corrupt" with
 * "this disk is full" will eventually delete the wrong thing.
 */
enum class ETerrainStorageResult : uint8
{
	Ok = 0,
	NotFound,
	AlreadyExists,
	BadPath,
	WrongSize,
	IoError,
};

TERRAINCORE_API const TCHAR* TerrainStorageResultName(ETerrainStorageResult Result);

// ---- path rules (P-004 §11.1) -------------------------------------------

/**
 * Every path a device accepts is relative, forward-slashed, and made of a restricted
 * character set. Absolute paths, drive letters, backslashes, `.` and `..` components, leading
 * or doubled separators and control characters are all refused.
 *
 * This is the second half of P-004 §11.1's rule. The first half is that an object ID is
 * validated as exactly 64 lowercase hex characters before it becomes a path component
 * (`TerrainPersistDigestFromHex`); this is the backstop that makes a mistake in the first
 * half harmless rather than an arbitrary-file-write.
 */
TERRAINCORE_API bool TerrainStorageIsSafeRelativePath(const FString& RelativePath);

/** The layout of a world directory, from P-004 §11.1. Nothing else constructs these names. */
namespace TerrainStoragePaths
{
	inline constexpr const TCHAR* BaseDescriptor   = TEXT("base.tobj");
	inline constexpr const TCHAR* RootsDirectory   = TEXT("roots");
	inline constexpr const TCHAR* JournalDirectory = TEXT("journal");
	inline constexpr const TCHAR* ObjectsDirectory = TEXT("objects");

	/** roots/root.0 and roots/root.1 */
	TERRAINCORE_API FString RootSlot(int32 SlotIndex);
	/** journal/anchor.0 and journal/anchor.1 */
	TERRAINCORE_API FString AnchorSlot(int32 SlotIndex);
	/** journal/seg-%016llx.tjs */
	TERRAINCORE_API FString JournalSegment(uint64 SegmentId);
	/** objects/<first digest byte, lowercase hex>/<64 hex>.tobj -- a 256-way fan-out. */
	TERRAINCORE_API FString Object(const FTerrainDigest& Digest);
	/** The directory an object lives in, so a caller can create it before writing. */
	TERRAINCORE_API FString ObjectDirectory(const FTerrainDigest& Digest);
}

// ---- the device seam ----------------------------------------------------

/**
 * The whole durable-write surface of the terrain store. Six operations, on purpose: every one
 * of them is a place a crash can happen, and a smaller surface is a smaller crash matrix.
 *
 * DURABILITY CONTRACT. Every mutating call below must have reached stable storage by the time
 * it returns Ok, and implementations get that by calling `IFileHandle::Flush(true)`.
 *
 * **The `true` is load-bearing and the default is wrong for us.** `Flush`'s default argument
 * is `false`, which the interface documents as letting "the operating/file system have more
 * leeway about when the data actually gets written to disk". On Windows the parameter is
 * ignored and `Flush` always calls `FlushFileBuffers`. On Unix it is not ignored:
 * `Flush(false)` is `fdatasync` and `Flush(true)` is `fsync`, and `fdatasync` does not
 * guarantee that **metadata** -- including the file's new length -- is durable. Every object
 * write and every journal append extends a file, and the Linux dedicated server is the
 * shipping target (D-002). A defaulted `Flush()` would therefore be correct on the machine
 * this is developed on and wrong on the machine it ships on, which is the worst shape a bug
 * can have. See P-004 §12.
 *
 * None of this makes *namespace* publication durable; that is R-015 and remains unproved.
 */
class TERRAINCORE_API ITerrainStorageDevice
{
public:
	virtual ~ITerrainStorageDevice() = default;

	/** Creates the directory and any parents. Ok if it already exists. */
	virtual ETerrainStorageResult EnsureDirectory(const FString& RelativePath) = 0;

	virtual bool  Exists(const FString& RelativePath) const = 0;
	/** Size in bytes, or -1 when absent. */
	virtual int64 Size(const FString& RelativePath) const = 0;

	virtual ETerrainStorageResult Read(const FString& RelativePath, TArray<uint8>& OutBytes) const = 0;

	/**
	 * Creates a file that does not exist, writes it, flushes it, closes it.
	 *
	 * Refuses with AlreadyExists rather than overwriting. Immutable objects are immutable:
	 * an object whose digest already exists has identical content by construction, and a
	 * caller that wants to rewrite one has misunderstood something.
	 */
	virtual ETerrainStorageResult WriteNew(const FString& RelativePath, TArrayView<const uint8> Bytes) = 0;

	/**
	 * Overwrites an existing file of exactly the same length, in place, and flushes.
	 *
	 * **Does not truncate.** Opening for write with truncation would make the file briefly
	 * zero-length, so a crash in the window would lose a slot that the whole publication
	 * protocol depends on being either old or new, never absent. A length mismatch is
	 * WrongSize, because the only files published this way are the four fixed 4096-byte slots.
	 */
	virtual ETerrainStorageResult OverwriteInPlace(const FString& RelativePath, TArrayView<const uint8> Bytes) = 0;

	/** Appends to an existing file and flushes. The journal's only mutating operation. */
	virtual ETerrainStorageResult Append(const FString& RelativePath, TArrayView<const uint8> Bytes) = 0;

	virtual ETerrainStorageResult Delete(const FString& RelativePath) = 0;
};

/** The real device. Rooted at an absolute directory; nothing it does can escape that root. */
class TERRAINCORE_API FTerrainPlatformStorageDevice final : public ITerrainStorageDevice
{
public:
	explicit FTerrainPlatformStorageDevice(const FString& InAbsoluteRoot);

	virtual ETerrainStorageResult EnsureDirectory(const FString& RelativePath) override;
	virtual bool  Exists(const FString& RelativePath) const override;
	virtual int64 Size(const FString& RelativePath) const override;
	virtual ETerrainStorageResult Read(const FString& RelativePath, TArray<uint8>& OutBytes) const override;
	virtual ETerrainStorageResult WriteNew(const FString& RelativePath, TArrayView<const uint8> Bytes) override;
	virtual ETerrainStorageResult OverwriteInPlace(const FString& RelativePath, TArrayView<const uint8> Bytes) override;
	virtual ETerrainStorageResult Append(const FString& RelativePath, TArrayView<const uint8> Bytes) override;
	virtual ETerrainStorageResult Delete(const FString& RelativePath) override;

	const FString& GetRoot() const { return Root; }

private:
	/** Returns an empty string when RelativePath is not safe, which every caller checks. */
	FString Resolve(const FString& RelativePath) const;

	FString Root;
};

/** An in-memory device. Same contract, no file system, so the format tests stay headless. */
class TERRAINCORE_API FTerrainMemoryStorageDevice final : public ITerrainStorageDevice
{
public:
	virtual ETerrainStorageResult EnsureDirectory(const FString& RelativePath) override;
	virtual bool  Exists(const FString& RelativePath) const override;
	virtual int64 Size(const FString& RelativePath) const override;
	virtual ETerrainStorageResult Read(const FString& RelativePath, TArray<uint8>& OutBytes) const override;
	virtual ETerrainStorageResult WriteNew(const FString& RelativePath, TArrayView<const uint8> Bytes) override;
	virtual ETerrainStorageResult OverwriteInPlace(const FString& RelativePath, TArrayView<const uint8> Bytes) override;
	virtual ETerrainStorageResult Append(const FString& RelativePath, TArrayView<const uint8> Bytes) override;
	virtual ETerrainStorageResult Delete(const FString& RelativePath) override;

	int32 NumFiles() const { return Files.Num(); }
	void  GetPaths(TArray<FString>& Out) const { Files.GetKeys(Out); }

	/** Direct access, for a test that needs to damage a file the way a disk would. */
	TArray<uint8>* Find(const FString& RelativePath) { return Files.Find(RelativePath); }

private:
	TMap<FString, TArray<uint8>> Files;
	TSet<FString> Directories;
};

// ---- fault injection (P-003 §8, "mandatory injected failures") ----------

/** Which operation a fault applies to. */
enum class ETerrainStorageOp : uint8
{
	EnsureDirectory = 0,
	Read,
	WriteNew,
	OverwriteInPlace,
	Append,
	Delete,
	Count,
};

/**
 * A decorator that fails, or tears, a chosen operation.
 *
 * `FailAfter` counts matching operations and fails the Nth. `TearBytes` instead lets the write
 * happen but keeps only the first N bytes — a torn write, which is the failure the two-slot
 * and torn-tail protocols exist to survive and which a simple "return IoError" would never
 * exercise. A torn write still counts as the operation, and still returns IoError, because a
 * caller that was told the write failed and finds half of it on disk is exactly the case.
 */
class TERRAINCORE_API FTerrainFaultDevice final : public ITerrainStorageDevice
{
public:
	explicit FTerrainFaultDevice(ITerrainStorageDevice& InInner) : Inner(InInner) {}

	/** Fail the CountBefore+1'th matching operation. Negative disables. */
	void FailAfter(ETerrainStorageOp Op, int32 CountBefore, const FString& PathFilter = FString());
	/** As FailAfter, but write only the first TearBytes bytes first. */
	void TearAfter(ETerrainStorageOp Op, int32 CountBefore, int32 TearBytes, const FString& PathFilter = FString());
	void ClearFaults();

	int32 OpCount(ETerrainStorageOp Op) const { return Counts[static_cast<int32>(Op)]; }

	virtual ETerrainStorageResult EnsureDirectory(const FString& RelativePath) override;
	virtual bool  Exists(const FString& RelativePath) const override;
	virtual int64 Size(const FString& RelativePath) const override;
	virtual ETerrainStorageResult Read(const FString& RelativePath, TArray<uint8>& OutBytes) const override;
	virtual ETerrainStorageResult WriteNew(const FString& RelativePath, TArrayView<const uint8> Bytes) override;
	virtual ETerrainStorageResult OverwriteInPlace(const FString& RelativePath, TArrayView<const uint8> Bytes) override;
	virtual ETerrainStorageResult Append(const FString& RelativePath, TArrayView<const uint8> Bytes) override;
	virtual ETerrainStorageResult Delete(const FString& RelativePath) override;

private:
	struct FFault
	{
		int32   Remaining = -1;   // -1 = inactive
		int32   TearBytes = -1;   // -1 = no partial write
		FString PathFilter;
	};

	/** True when this operation should fail; consumes the fault. */
	bool ShouldFail(ETerrainStorageOp Op, const FString& Path, int32& OutTearBytes);

	ITerrainStorageDevice& Inner;
	FFault Faults[static_cast<int32>(ETerrainStorageOp::Count)];
	mutable int32 Counts[static_cast<int32>(ETerrainStorageOp::Count)] = {};
};

// ---- the content-addressed object store ---------------------------------

/**
 * Immutable objects on a device, named by their BLAKE3 digest and nothing else.
 *
 * Verifies the digest **on the way in and on the way out**. Content addressing that is only
 * checked on write is a naming convention; checked on read as well, it is the property that a
 * reader can trust the bytes it was handed without trusting the medium they came from.
 *
 * Storing a digest that already exists is a success and writes nothing: two callers producing
 * the same object produced the same bytes, by construction.
 */
class TERRAINCORE_API FTerrainFileObjectStore final : public ITerrainObjectStore
{
public:
	explicit FTerrainFileObjectStore(ITerrainStorageDevice& InDevice) : Device(InDevice) {}

	virtual bool LoadObject(const FTerrainDigest& Digest, TArray<uint8>& OutBytes) const override;
	virtual bool StoreObject(const FTerrainDigest& Digest, TArrayView<const uint8> Bytes) override;

	/** Creates `objects/`. The per-prefix directories are created on demand by StoreObject. */
	ETerrainStorageResult EnsureLayout();

	bool Contains(const FTerrainDigest& Digest) const;

	/** Removes an object. Retention decides WHAT to delete; this only performs it. */
	ETerrainStorageResult DeleteObject(const FTerrainDigest& Digest);

private:
	ITerrainStorageDevice& Device;
};

// ---- the slot pair ------------------------------------------------------

/** What one slot of a pair looks like after a read. */
struct FTerrainSlotState
{
	bool                 bPresent = false;
	bool                 bValid = false;
	ETerrainPersistError Error = ETerrainPersistError::None;
	uint64               Generation = 0;
	TArray<uint8>        Body;   // exactly TerrainPersistSlotBodySize when bValid
};

/**
 * Two pre-created fixed-size slot files, published alternately (P-004 §8, §9.5).
 *
 * The publication primitive of the whole store. It creates no new names at runtime, which is
 * the one part of P-004 §12's durability argument that does not depend on the unproved
 * namespace question (R-015): whatever happens to a new object's directory entry, the thing
 * that decides *which state is current* was overwritten in place in a file that already
 * existed.
 *
 * Read() picks the highest generation among slots that fully validate, and reports both
 * slots' states so a caller can refuse to reclaim anything while redundancy is broken --
 * P-004 §8 requires exactly that. Publish() writes the slot that is NOT the current best, so
 * a torn publication can never damage the state the store would otherwise fall back to.
 *
 * The generation is supplied by the caller rather than parsed here, because a root slot and
 * an anchor slot carry it in different fields and this class is deliberately ignorant of both
 * bodies. It validates framing and identity; the caller validates meaning.
 */
class TERRAINCORE_API FTerrainSlotPair
{
public:
	FTerrainSlotPair(ITerrainStorageDevice& InDevice,
	                 ETerrainPersistObjectType InType,
	                 const FString& InSlotZero,
	                 const FString& InSlotOne)
		: Device(InDevice), Type(InType)
	{
		Paths[0] = InSlotZero;
		Paths[1] = InSlotOne;
	}

	/** Creates both slots from an initial body. Refuses if either already exists. */
	ETerrainStorageResult Create(const FTerrainPersistIdentity& Identity, TArrayView<const uint8> InitialBody);

	/**
	 * Reads and validates both slots.
	 *
	 * OutBestIndex is the slot to trust, or INDEX_NONE when neither validates. Reading also
	 * arms Publish(), which writes the other one.
	 */
	ETerrainStorageResult Read(const FTerrainPersistIdentity& Identity,
	                           FTerrainSlotState& OutSlotZero,
	                           FTerrainSlotState& OutSlotOne,
	                           int32& OutBestIndex);

	/**
	 * Publishes a new body to the inactive slot.
	 *
	 * Read() must have been called first: publishing without knowing which slot is current
	 * risks overwriting the only good copy, so this refuses rather than guessing.
	 */
	ETerrainStorageResult Publish(const FTerrainPersistIdentity& Identity, TArrayView<const uint8> Body);

	/** The slot index Publish() would write next, or INDEX_NONE before a Read(). */
	int32 GetNextPublishIndex() const { return NextPublishIndex; }

	const FString& GetPath(int32 SlotIndex) const { return Paths[SlotIndex]; }

private:
	ITerrainStorageDevice&    Device;
	ETerrainPersistObjectType Type;
	FString                   Paths[2];
	int32                     NextPublishIndex = INDEX_NONE;
	bool                      bHasRead = false;
};

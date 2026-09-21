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
	inline constexpr const TCHAR* PacksDirectory   = TEXT("packs");
	inline constexpr const TCHAR* ContainersDirectory = TEXT("containers");

	/**
	 * The fixed container pool (P-005 §4). Every object written after world creation goes into
	 * one of these pre-created files; none is ever created or removed at runtime.
	 */
	inline constexpr int32 ContainerCount = 4;

	/** containers/c.0 .. containers/c.3 */
	TERRAINCORE_API FString Container(int32 ContainerIndex);

	/** roots/root.0 and roots/root.1 */
	TERRAINCORE_API FString RootSlot(int32 SlotIndex);
	/** journal/anchor.0 and journal/anchor.1 */
	TERRAINCORE_API FString AnchorSlot(int32 SlotIndex);
	/** journal/seg-%016llx.tjs */
	TERRAINCORE_API FString JournalSegment(uint64 SegmentId);

	/**
	 * The inverse of JournalSegment, over a bare file name.
	 *
	 * Lives beside the function that PRODUCES the name, because a reader and a writer that
	 * each keep their own idea of the name shape are two things that must agree and have no
	 * mechanism to. Refuses anything that is not exactly `seg-` + 16 lowercase hex + `.tjs`.
	 */
	TERRAINCORE_API bool ParseJournalSegment(const FString& FileName, uint64& OutSegmentId);
	/**
	 * objects/<first digest byte, lowercase hex>/<64 hex>.tobj -- a 256-way fan-out.
	 *
	 * Pre-P-005 layout. Read and migrated by retention; never written any more.
	 */
	TERRAINCORE_API FString Object(const FTerrainDigest& Digest);
	/** The directory an object lives in, so a caller can create it before writing. */
	TERRAINCORE_API FString ObjectDirectory(const FTerrainDigest& Digest);

	/** packs/pack-%016llx.tpk -- pre-P-005 layout, read and migrated, never written. */
	TERRAINCORE_API FString Pack(uint64 PackId);

	/** The inverse of Pack, over a bare file name -- same reason as ParseJournalSegment. */
	TERRAINCORE_API bool ParsePack(const FString& FileName, uint64& OutPackId);
}

// ---- the device seam ----------------------------------------------------

/**
 * The whole durable-write surface of the terrain store. Seven mutating operations, on purpose:
 * every one of them is a place a crash can happen, and a smaller surface is a smaller crash matrix.
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
 * None of this makes *namespace* publication durable, and after P-005 nothing needs it to: an
 * open world creates and removes no names. WriteNew is bootstrap-only (world creation, and
 * pre-creating the container pool); Delete is used only to remove pre-P-005 files whose
 * contents are already durable elsewhere. See P-005 §2, §6.
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
	 * Lists the file names directly inside a directory -- names only, not paths, not recursive.
	 *
	 * Journal segment discovery needs it: P-003 §3 requires boot to tell an anchored segment
	 * from a newer **unanchored** one, and to tell an orphan (created, never written) from a
	 * protocol violation (created, written, never anchored). Neither is knowable without
	 * looking at what is actually on disk.
	 *
	 * A missing directory is NotFound, not an empty list. "There is no journal directory" and
	 * "the journal directory is empty" are different facts about a world.
	 */
	virtual ETerrainStorageResult ListFiles(const FString& RelativeDirectory, TArray<FString>& OutNames) const = 0;

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

	/**
	 * Reads exactly Length bytes starting at Offset. A range not wholly inside the file is
	 * WrongSize. Resolving one object must not read a whole container (P-005 §3).
	 */
	virtual ETerrainStorageResult ReadRange(
		const FString& RelativePath, int64 Offset, int64 Length, TArray<uint8>& OutBytes) const = 0;

	/**
	 * Shrinks an existing file to NewSize, then flushes.
	 *
	 * **Shrink only**: a NewSize beyond the current length is WrongSize, because a truncation
	 * that could grow a file would be a way to write bytes nobody chose. This changes one
	 * existing file's length -- the same kind of metadata an Append changes, and made durable by
	 * the same per-file flush the journal already depends on (P-005 §3). It never touches a name.
	 */
	virtual ETerrainStorageResult Truncate(const FString& RelativePath, int64 NewSize) = 0;

	/**
	 * Makes a directory's entries durable, as far as the platform allows. **Bootstrap only**
	 * (P-005 §6): called after world creation, never on a path an acknowledged edit waits for.
	 *
	 * Unix: `fsync` on the directory, POSIX's documented primitive; failure is IoError.
	 * Windows: `FlushFileBuffers` on a backup-semantics directory handle. Not a documented
	 * guarantee, so a failure is logged and reported as Ok rather than refusing to create worlds.
	 */
	virtual ETerrainStorageResult SyncDirectory(const FString& RelativeDirectory) = 0;
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
	virtual ETerrainStorageResult ListFiles(const FString& RelativeDirectory, TArray<FString>& OutNames) const override;
	virtual ETerrainStorageResult WriteNew(const FString& RelativePath, TArrayView<const uint8> Bytes) override;
	virtual ETerrainStorageResult OverwriteInPlace(const FString& RelativePath, TArrayView<const uint8> Bytes) override;
	virtual ETerrainStorageResult Append(const FString& RelativePath, TArrayView<const uint8> Bytes) override;
	virtual ETerrainStorageResult Delete(const FString& RelativePath) override;
	virtual ETerrainStorageResult ReadRange(const FString& RelativePath, int64 Offset, int64 Length, TArray<uint8>& OutBytes) const override;
	virtual ETerrainStorageResult Truncate(const FString& RelativePath, int64 NewSize) override;
	virtual ETerrainStorageResult SyncDirectory(const FString& RelativeDirectory) override;

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
	virtual ETerrainStorageResult ListFiles(const FString& RelativeDirectory, TArray<FString>& OutNames) const override;
	virtual ETerrainStorageResult WriteNew(const FString& RelativePath, TArrayView<const uint8> Bytes) override;
	virtual ETerrainStorageResult OverwriteInPlace(const FString& RelativePath, TArrayView<const uint8> Bytes) override;
	virtual ETerrainStorageResult Append(const FString& RelativePath, TArrayView<const uint8> Bytes) override;
	virtual ETerrainStorageResult Delete(const FString& RelativePath) override;
	virtual ETerrainStorageResult ReadRange(const FString& RelativePath, int64 Offset, int64 Length, TArray<uint8>& OutBytes) const override;
	virtual ETerrainStorageResult Truncate(const FString& RelativePath, int64 NewSize) override;
	virtual ETerrainStorageResult SyncDirectory(const FString& RelativeDirectory) override;

	int32 NumFiles() const { return Files.Num(); }
	void  GetPaths(TArray<FString>& Out) const { Files.GetKeys(Out); }

	/** Every byte the device holds. How a test says "the store actually got smaller". */
	int64 TotalBytes() const
	{
		int64 Total = 0;
		for (const TPair<FString, TArray<uint8>>& File : Files) { Total += File.Value.Num(); }
		return Total;
	}

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
	ListFiles,
	WriteNew,
	OverwriteInPlace,
	Append,
	Delete,
	Truncate,
	SyncDirectory,
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

	/**
	 * Fails the Nth **mutating** operation of the session, counting WriteNew, OverwriteInPlace,
	 * Append, Delete and Truncate together as one sequence (P-003 §8).
	 *
	 * The per-op-type faults above can crash a chosen kind of write. This crashes a chosen
	 * *moment*, which is what a crash actually is: a session does its writes in one order, and
	 * the matrix walks that order one index at a time. Without it, "inject at every write and
	 * flush point" has to be approximated by a handful of hand-picked cases, and the points
	 * nobody thought of are exactly the ones that break.
	 *
	 * `TearBytes >= 0` truncates that write instead of skipping it, which is the failure the
	 * two-slot and torn-tail protocols exist to survive. Negative index disables.
	 */
	void FailAtMutation(int32 Index, int32 TearBytes = -1);

	/** Mutating operations attempted so far, over all five kinds. */
	int32 MutationCount() const { return Mutations; }

	int32 OpCount(ETerrainStorageOp Op) const { return Counts[static_cast<int32>(Op)]; }

	virtual ETerrainStorageResult EnsureDirectory(const FString& RelativePath) override;
	virtual bool  Exists(const FString& RelativePath) const override;
	virtual int64 Size(const FString& RelativePath) const override;
	virtual ETerrainStorageResult Read(const FString& RelativePath, TArray<uint8>& OutBytes) const override;
	virtual ETerrainStorageResult ListFiles(const FString& RelativeDirectory, TArray<FString>& OutNames) const override;
	virtual ETerrainStorageResult WriteNew(const FString& RelativePath, TArrayView<const uint8> Bytes) override;
	virtual ETerrainStorageResult OverwriteInPlace(const FString& RelativePath, TArrayView<const uint8> Bytes) override;
	virtual ETerrainStorageResult Append(const FString& RelativePath, TArrayView<const uint8> Bytes) override;
	virtual ETerrainStorageResult Delete(const FString& RelativePath) override;
	virtual ETerrainStorageResult ReadRange(const FString& RelativePath, int64 Offset, int64 Length, TArray<uint8>& OutBytes) const override;
	virtual ETerrainStorageResult Truncate(const FString& RelativePath, int64 NewSize) override;
	virtual ETerrainStorageResult SyncDirectory(const FString& RelativeDirectory) override;

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

	int32 Mutations = 0;             // WriteNew + OverwriteInPlace + Append + Delete + Truncate, in order
	int32 MutationFaultIndex = -1;   // which one to fail; -1 disables
	int32 MutationTearBytes = -1;
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
 *
 * WHERE THE BYTES LIVE (P-005). Every object is written into one of a fixed pool of
 * pre-created **containers**, as part of a **frame** whose body is a P-004 §13.3 pack image.
 * A capture is still one durable write -- one Append, one flush (D-036) -- but it now extends a
 * file that already exists instead of creating a new one, so an open world never depends on a
 * directory entry surviving a power cut (R-015). Pre-P-005 loose objects and pack files are
 * still read, and retention migrates them into containers; nothing writes them any more.
 */
class TERRAINCORE_API FTerrainFileObjectStore final : public ITerrainObjectStore
{
public:
	explicit FTerrainFileObjectStore(ITerrainStorageDevice& InDevice) : Device(InDevice) {}

	virtual bool LoadObject(const FTerrainDigest& Digest, TArray<uint8>& OutBytes) const override;

	/**
	 * Inside a batch, buffers. Outside one, writes a one-object frame immediately: an unbatched
	 * store used to create a loose file, which was a runtime name (P-005 §9 item 6).
	 */
	virtual bool StoreObject(const FTerrainDigest& Digest, TArrayView<const uint8> Bytes) override;

	/**
	 * Creates `containers/` and any missing container. **The only place this store creates a
	 * name**, and it is bootstrap: world creation, or opening a pre-P-005 world, before the
	 * store admits anything (P-005 §6). OutCreated says whether anything was created, so the
	 * caller knows a directory sync is owed.
	 */
	ETerrainStorageResult EnsureLayout(bool* OutCreated = nullptr);

	bool Contains(const FTerrainDigest& Digest) const;

	// ---- batching: many objects, one durable write (P-004 §13) -------------------------

	/**
	 * Buffers subsequent StoreObject calls instead of writing each on its own.
	 *
	 * **Why this exists, measured rather than assumed.** A durable write is one `fsync` and an
	 * `fsync` costs about 3 ms on the development disk *regardless of size*. A checkpoint over 8
	 * chunks writes 59 objects; writing them one per file cost 0.168 s, and the same bytes in
	 * **one write with one flush cost 2.3 ms**. The fix is not *when* the store syncs, it is how
	 * many times. Content addressing is unchanged; only where the bytes live changes.
	 */
	void BeginBatch();
	bool IsBatchOpen() const { return bBatchOpen; }
	int32 BatchNum() const { return BatchEntries.Num(); }
	int64 BatchBytes() const { return BatchBuffer.Num(); }

	/**
	 * Appends everything buffered as one frame to the active container, flushes once, and closes
	 * the batch.
	 *
	 * Ok and writes nothing when the batch is empty. **Until this returns Ok, nothing buffered
	 * is durable** -- which is exactly the property publication needs: a frame no root slot
	 * names is unreferenced garbage.
	 *
	 * Before appending, a container whose length differs from its valid end is truncated back
	 * to it, and the append is refused if that fails (P-005 §4.3). A frame appended after torn
	 * bytes would be durable, named by a root, and invisible to the next boot's scan.
	 */
	ETerrainStorageResult CommitBatch();

	/** Discards the batch without writing. Nothing buffered was ever durable. */
	void AbandonBatch();

	/**
	 * Scans every container and every pre-P-005 pack, and builds the digest -> location map.
	 * Must run before any read that could resolve into either, so `FTerrainWorldStore::Open`
	 * runs it. **Mutates nothing**: a torn container tail is repaired lazily by the next append.
	 *
	 * A frame or pack that fails validation is **ignored, not an error**: it was written by an
	 * append whose flush never returned, so no published root refers to it. In a container the
	 * first invalid frame also ends the scan (P-005 §4.2).
	 */
	ETerrainStorageResult LoadPacks();

	/** Distinct digests resolvable through containers or pre-P-005 packs. */
	int32 NumPackedObjects() const;

	// ---- containers (P-005 §4) ---------------------------------------------------------

	int32 GetActiveContainer() const { return ActiveContainer; }

	/** Bytes of whole valid frames, from offset 0. Past it is a torn tail, or nothing. */
	int64 ContainerValidEnd(int32 ContainerIndex) const;

	/** Digests whose resolving copy lives in this container. */
	void GetContainerContents(int32 ContainerIndex, TArray<FTerrainDigest>& OutDigests) const;

	/**
	 * Object bytes in this container that are not the resolving copy of a Live digest: dead
	 * objects, and copies shadowed by an earlier container. Frame and manifest overhead is not
	 * counted, so a container holding only live objects reports zero and is left alone.
	 */
	int64 ContainerDeadBytes(int32 ContainerIndex, const TSet<FTerrainDigest>& Live) const;

	/**
	 * Makes an empty container active, if there is one and the active container is not already
	 * empty. Policy, not correctness: any container is a valid home for any frame (P-005 §4.5).
	 */
	bool RotateActiveToEmpty();

	/**
	 * Copies every Live object resolving into this container into one new frame in the active
	 * container, flushes it, reads every copy back through the verifying path, and only then
	 * truncates this container to zero (P-005 §5 step 3).
	 *
	 * Refuses the active container (it would copy into itself and then cut itself), and refuses
	 * while a batch is open. A failure before the truncation changes nothing that matters: the
	 * originals are untouched, and a copy that became durable is a harmless duplicate.
	 */
	ETerrainStorageResult CompactContainer(
		int32 ContainerIndex, const TSet<FTerrainDigest>& Live, int32& OutKept, int32& OutDropped);

	// ---- pre-P-005 files: read and migrate, never write --------------------------------

	/**
	 * Every loose object currently on the device, by digest. Walks the 256-way fan-out, so it
	 * is a reclamation-time operation. A file whose name is not a valid digest is skipped.
	 */
	ETerrainStorageResult ListLooseObjects(TArray<FTerrainDigest>& OutDigests) const;

	/** Every pre-P-005 pack id on the device, ascending, readable or not. */
	ETerrainStorageResult ListPacks(TArray<uint64>& OutPackIds) const;

	/**
	 * Copies every Live object that currently resolves through a loose file or a pre-P-005
	 * pack into one frame in the active container and flushes it; **then** deletes every
	 * loose object and every pre-P-005 pack (P-005 §5 step 1).
	 *
	 * Removing a name is safe where creating one is not: if a removal is lost to a power cut,
	 * the file comes back holding a byte-identical duplicate.
	 */
	ETerrainStorageResult MigrateLegacy(const TSet<FTerrainDigest>& Live,
		int32& OutMigrated, int32& OutFilesDeleted, int64& OutBytesDeleted);

private:
	struct FObjectLocation
	{
		int64 Offset = 0;   // absolute, within the container or pack file
		int32 Length = 0;
	};

	struct FContainerState
	{
		int64 ValidEnd = 0;
		int64 ObjectBytes = 0;   // every accepted manifest entry, duplicates included
		TMap<FTerrainDigest, FObjectLocation> Index;   // first copy within this container
	};

	/** Validates a pack image (P-004 §13.4 rules 1-5); entry offsets are relative to it. */
	static bool ParsePackImage(TArrayView<const uint8> Image,
		TArray<TPair<FTerrainDigest, FObjectLocation>>& OutEntries);

	/** Builds a pack image from the open batch. */
	void BuildBatchImage(TArray<uint8>& OutImage) const;

	ETerrainStorageResult ScanContainer(int32 ContainerIndex);
	ETerrainStorageResult LoadLegacyPacks();

	bool LoadFrom(const FString& Path, const FObjectLocation& Where,
		const FTerrainDigest& Digest, TArray<uint8>& OutBytes) const;

	/** The container a digest resolves into -- the first, in index order -- or INDEX_NONE. */
	int32 ResolveContainer(const FTerrainDigest& Digest) const;

	ITerrainStorageDevice& Device;

	FContainerState Containers[TerrainStoragePaths::ContainerCount];
	int32 ActiveContainer = 0;

	/** Pre-P-005 packs: digest -> (pack id, location). Read-only. */
	TMap<FTerrainDigest, TPair<uint64, FObjectLocation>> LegacyPacks;

	bool          bBatchOpen = false;
	TArray<uint8> BatchBuffer;
	TArray<FTerrainDigest> BatchOrder;                    // store order, so frames are deterministic
	TMap<FTerrainDigest, FObjectLocation> BatchEntries;   // offsets are into BatchBuffer
};

/**
 * A pack's fixed 32-byte trailer, at the very end of the image (P-004 §13).
 *
 * The trailer is last because it is written last: a torn pack has no valid trailer, so
 * "complete" and "usable" are the same question and one read at a known offset answers it.
 */
inline constexpr uint64 TerrainPackMagic        = 0x314B4341504E5254ULL;  // "TRNPACK1", little-endian
inline constexpr uint32 TerrainPackVersion      = 1;
inline constexpr int32  TerrainPackTrailerSize  = 32;
inline constexpr int32  TerrainPackEntrySize    = 44;   // 32-byte digest + u64 offset + u32 length
inline constexpr int32  TerrainPackMaxEntries   = 1 << 20;

/**
 * A container frame's fixed 40-byte header (P-005 §4.1). The body that follows is a pack image.
 *
 * The header records its own offset, so a frame is valid only where it was written: the
 * remains of an older frame beyond a truncation point can never be read as a frame.
 */
inline constexpr uint64 TerrainFrameMagic       = 0x314D415246535254ULL;  // "TRSFRAM1", little-endian
inline constexpr uint32 TerrainFrameVersion     = 1;
inline constexpr int32  TerrainFrameHeaderSize  = 40;
/** A frame body beyond this is refused on write and treated as invalid on read. */
inline constexpr int64  TerrainFrameMaxBody     = 1LL << 30;

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

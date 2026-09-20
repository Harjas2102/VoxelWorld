// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "TerrainPersistenceFormat.h"
#include "TerrainChunk.h"
#include "TerrainTypes.h"

/**
 * TerrainPersistenceIndex.h -- the 96-bit chunk-key radix index (Docs/proposals/P-004, 6).
 *
 * P-003 section 4 replaced the flat complete manifest with this: a persistent, immutable,
 * path-copied index whose per-checkpoint work is proportional to the number of CHANGED keys
 * rather than to the number of chunks ever edited. That is the whole argument for why Pillar 1
 * ("the world permanently records what the players did to it") does not cost a full rewrite of
 * cold history every time a checkpoint is published.
 *
 * A checkpoint that changes D keys writes at most 12*D pages before shared-prefix coalescing;
 * every untouched subtree is referenced by the digest it already had and is not read, rewritten
 * or re-hashed.
 *
 * The object store seam below is deliberately tiny and deliberately in-memory-first: the index
 * is pure, so it tests headless with no file system, and the file-backed store is a later
 * increment that implements the same two methods.
 */

// ---- 6.1 key transform --------------------------------------------------

/**
 * A chunk key as 12 index bytes.
 *
 * Each signed coordinate is transformed by flipping its sign bit and written BIG-ENDIAN, then
 * X || Y || Z. That combination makes unsigned byte-wise comparison of keys agree exactly with
 * signed component-wise comparison, which is what lets a radix page hold a sorted, uniquely
 * prefixed child set -- and it is why the transform is not simply a memcpy of three int32s.
 */
struct FTerrainIndexKey
{
	uint8 Bytes[TerrainPersistIndexKeyBytes] = {};

	friend bool operator==(const FTerrainIndexKey& A, const FTerrainIndexKey& B)
	{
		return FMemory::Memcmp(A.Bytes, B.Bytes, TerrainPersistIndexKeyBytes) == 0;
	}
	friend bool operator!=(const FTerrainIndexKey& A, const FTerrainIndexKey& B) { return !(A == B); }
	friend bool operator< (const FTerrainIndexKey& A, const FTerrainIndexKey& B)
	{
		return FMemory::Memcmp(A.Bytes, B.Bytes, TerrainPersistIndexKeyBytes) < 0;
	}
};

TERRAINCORE_API FTerrainIndexKey  TerrainIndexKeyFromChunk(const FTerrainChunkKey& Key);
TERRAINCORE_API FTerrainChunkKey  TerrainChunkKeyFromIndex(const FTerrainIndexKey& Key);

// ---- 6.2 pages ----------------------------------------------------------

inline constexpr int32 TerrainIndexPageHeaderBytes   = 16;
inline constexpr int32 TerrainIndexInternalEntryBytes = 37;
inline constexpr int32 TerrainIndexLeafEntryBytes     = 50;
/** Depth 11 is the leaf level; depths 0..10 are internal. Traversal is exactly 12 bytes. */
inline constexpr int32 TerrainIndexLeafDepth = TerrainPersistIndexKeyBytes - 1;

struct FTerrainIndexInternalEntry
{
	uint8          ByteValue = 0;
	FTerrainDigest ChildDigest;
	uint32         ChildLength = 0;
};

/** What the index stores per chunk. Empty chunks live here and nowhere else (P-004 5.2). */
struct FTerrainIndexLeafValue
{
	ETerrainRegionEncoding Encoding = ETerrainRegionEncoding::Empty;
	FTerrainRev            Rev = 0;
	FTerrainOpSeq          LastOpSeq = 0;
	uint32                 PayloadLength = 0;   // 0 iff Empty
	FTerrainDigest         PayloadDigest;       // zero iff Empty
};

struct FTerrainIndexLeafEntry
{
	uint8                  ByteValue = 0;
	FTerrainIndexLeafValue Value;
};

struct FTerrainIndexPage
{
	uint8 Depth = 0;
	bool  bLeaf = false;
	uint8 KeyPrefix[TerrainPersistIndexKeyBytes] = {};

	TArray<FTerrainIndexInternalEntry> Internal;   // when !bLeaf
	TArray<FTerrainIndexLeafEntry>     Leaves;     // when bLeaf
};

TERRAINCORE_API ETerrainPersistError TerrainIndexEncodePageBody(
	const FTerrainIndexPage& In, TArray<uint8>& OutBody);

TERRAINCORE_API ETerrainPersistError TerrainIndexDecodePageBody(
	TArrayView<const uint8> Body, FTerrainIndexPage& Out);

// ---- the object store seam ---------------------------------------------

/**
 * Immutable content-addressed objects, read by digest.
 *
 * Two methods, on purpose. A file-backed implementation arrives with the storage owner in a
 * later increment; it derives its path from the digest by the single fixed rule in P-004
 * section 11.1 and verifies the loaded content against that digest. Nothing in this header
 * knows what a path is.
 */
class TERRAINCORE_API ITerrainObjectStore
{
public:
	virtual ~ITerrainObjectStore() = default;

	/** Loads the complete object image (header + body) for Digest. False when absent. */
	virtual bool LoadObject(const FTerrainDigest& Digest, TArray<uint8>& OutBytes) const = 0;

	/** Stores an object image. Implementations verify that it actually hashes to Digest. */
	virtual bool StoreObject(const FTerrainDigest& Digest, TArrayView<const uint8> Bytes) = 0;
};

/** The headless implementation the tests and the checkpoint builder use. */
class TERRAINCORE_API FTerrainMemoryObjectStore final : public ITerrainObjectStore
{
public:
	virtual bool LoadObject(const FTerrainDigest& Digest, TArray<uint8>& OutBytes) const override;
	virtual bool StoreObject(const FTerrainDigest& Digest, TArrayView<const uint8> Bytes) override;

	bool  Contains(const FTerrainDigest& Digest) const { return Objects.Contains(Digest); }
	int32 Num() const { return Objects.Num(); }
	int64 TotalBytes() const;
	void  Reset() { Objects.Reset(); }

	/** Every digest currently held. Used by the path-copy test to prove sharing. */
	void GetDigests(TArray<FTerrainDigest>& Out) const;

private:
	TMap<FTerrainDigest, TArray<uint8>> Objects;
};

// ---- 6.3 path-copy apply, lookup and validation -------------------------

struct FTerrainIndexUpdate
{
	FTerrainChunkKey       Key;
	FTerrainIndexLeafValue Value;
};

struct FTerrainIndexRoot
{
	bool           bHasRootPage = false;
	FTerrainDigest RootPageDigest;
	uint32         RootPageLength = 0;
};

/**
 * Applies Updates to the tree at OldRoot, writing only the pages that changed.
 *
 * Every page on the path from the root to a changed leaf is rewritten as a NEW immutable
 * object; untouched siblings and subtrees are carried over by their existing digests. The
 * result is a complete independent tree that shares all cold storage with its predecessor,
 * which is what makes the global cut cheap without making it partial.
 *
 * Updates may arrive unsorted and are sorted internally; a repeated key is DuplicateKey rather
 * than last-write-wins, because "which of these two values did the caller mean" is not a
 * question a save format should answer by accident.
 *
 * There is no delete. A chunk that has been edited stays in the index forever, possibly as
 * Empty with its revision metadata -- P-003 section 6 is explicit that an edited chunk which
 * matches the base again is Empty, NOT absent, because absent means "never touched, regenerate
 * from the base" and those are different facts about the world.
 *
 * OutPagesWritten is the measured page count the 12*D bound is asserted against.
 */
TERRAINCORE_API ETerrainPersistError TerrainIndexApply(
	const FTerrainPersistIdentity& Identity,
	const ITerrainObjectStore& Source,
	ITerrainObjectStore& Sink,
	const FTerrainIndexRoot& OldRoot,
	const TArray<FTerrainIndexUpdate>& Updates,
	FTerrainIndexRoot& OutRoot,
	int32& OutPagesWritten);

/** Resolves one key. bOutFound false means never edited -- regenerate from the base. */
TERRAINCORE_API ETerrainPersistError TerrainIndexLookup(
	const FTerrainPersistIdentity& Identity,
	const ITerrainObjectStore& Source,
	const FTerrainIndexRoot& Root,
	const FTerrainChunkKey& Key,
	FTerrainIndexLeafValue& OutValue,
	bool& bOutFound);

struct FTerrainIndexValidation
{
	int64 PageCount = 0;
	int64 LeafCount = 0;
	int64 TotalPayloadBytes = 0;
	int32 MaxDepthSeen = 0;
};

/**
 * Walks the whole closure and checks every structural rule in P-004 section 6.2/6.3: declared
 * depth against traversal depth, KeyPrefix against the bytes actually traversed, ordering,
 * counts, lengths and leaf consistency.
 *
 * It does NOT verify payload content digests: that requires the payload objects themselves and
 * belongs to the eager boot validation P-003 section 4 specifies, which is the recovery
 * increment. Passing this function is a structural result and is not a bootable-world claim.
 * OutKeys, when supplied, receives every key in ascending order.
 */
TERRAINCORE_API ETerrainPersistError TerrainIndexValidate(
	const FTerrainPersistIdentity& Identity,
	const ITerrainObjectStore& Source,
	const FTerrainIndexRoot& Root,
	FTerrainIndexValidation& OutStats,
	TArray<FTerrainChunkKey>* OutKeys = nullptr);

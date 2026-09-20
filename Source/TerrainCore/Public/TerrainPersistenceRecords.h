// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "TerrainPersistenceFormat.h"
#include "TerrainChunk.h"
#include "TerrainOp.h"
#include "TerrainTypes.h"

/**
 * TerrainPersistenceRecords.h -- the schema-2 record bodies (Docs/proposals/P-004, 4-9).
 *
 * Every struct here is a decoded record, never the bytes. The bytes are produced and consumed
 * only by the functions below, which follow P-004's tables field by field. No struct in this
 * header is ever memcpy'd to disk and no encoder uses sizeof, so the on-disk format does not
 * depend on padding, alignment or field reordering by the compiler.
 *
 * The chunk-key index (P-004 section 6) is in TerrainPersistenceIndex.h.
 *
 * SCOPE, stated once: this file is codecs and validation. There is no file handle here, no
 * commit path, no checkpoint pump, no settlement and no recovery driver. The journal segment
 * scanner below reads a byte buffer and says what is in it; deciding what to DO about a torn
 * tail is the recovery increment's job, and it is not written.
 */

// ---- fixed sizes, all from P-004 ---------------------------------------
inline constexpr int32 TerrainPersistBaseDescriptorPrefix   = 122;   // section 4
inline constexpr int32 TerrainPersistChunkPayloadPrefix     = 32;    // section 5
inline constexpr int32 TerrainPersistDenseBytes             = TerrainChunkSampleCount * 4;   // 131072
inline constexpr int32 TerrainPersistSparseEntryBytes       = 6;
inline constexpr int32 TerrainPersistCheckpointBodySize     = 80;    // section 7
inline constexpr int32 TerrainPersistRootSlotFixedBytes     = 64;    // section 8, before the tail
inline constexpr int32 TerrainPersistSegmentHeaderBodySize  = TerrainPersistMaxJournalSegmentHeaderBody; // 72, section 9.1
inline constexpr int32 TerrainPersistRecordFrameOverhead    = 20;    // section 9.2
inline constexpr int32 TerrainPersistCommitRecordPrefix     = 140;   // section 9.3
inline constexpr int32 TerrainPersistSealRecordBodySize     = 64;    // section 9.4
inline constexpr int32 TerrainPersistAnchorFixedBytes       = 88;    // section 9.5, before the tail
inline constexpr int32 TerrainPersistPhysicalEntryBytes     = 10;
inline constexpr int32 TerrainPersistChangedKeyEntryBytes   = 20;
inline constexpr int32 TerrainPersistEconomyDeltaBytes      = 32;
inline constexpr int32 TerrainPersistTokenDigestBytes       = 16;

/** SparseDiff is smaller than Dense up to this many changed samples (P-004 section 5.3). */
inline constexpr int32 TerrainPersistSparseBreakEven = 21844;

// ---- 4. base descriptor -------------------------------------------------

/**
 * The exact world shape. Every other object's header binds to BLAKE3 of this body, so a
 * generator change that is not reflected here cannot silently reinterpret a saved chunk.
 *
 * NO FLOATING POINT (P-004 section 1 rule 2). VoxelSizeCm and the world origin are config
 * floats and cross as exact micrometres; a persisted `float` would make save compatibility
 * depend on 50.0f decoding identically on every toolchain, which is R-014's exact shape.
 */
struct FTerrainBaseDescriptor
{
	int64          Seed = 0;
	uint32         GeneratorVersion = 0;
	uint32         BackendKernelVersion = 0;
	/** BLAKE3 over the generator's own canonical parameter encoding (P-004 section 4.1). */
	FTerrainDigest GeneratorParamsDigest;
	int64          OriginWorldMicrometres[3] = {};
	int64          VoxelSizeMicrometres = 500000;   // 50 cm
	int32          ChunkSizeVox = TerrainChunkSizeVox;
	FTerrainBox    WorldBoundsVox;
	uint8          ValueConfig = 0;
	uint8          EncodingRulesVersion = 1;
	uint16         MaterialCatalogVersion = 0;
	uint16         MaterialCatalogCount = 0;
	FString        GeneratorName;
	FString        BackendName;
	TArray<FTerrainDigest> AuthoredStamps;
};

TERRAINCORE_API ETerrainPersistError TerrainPersistEncodeBaseDescriptorBody(
	const FTerrainBaseDescriptor& In, TArray<uint8>& OutBody);

TERRAINCORE_API ETerrainPersistError TerrainPersistDecodeBaseDescriptorBody(
	TArrayView<const uint8> Body, FTerrainBaseDescriptor& Out);

/**
 * Encodes the base descriptor object AND returns the BaseDigest every other object carries.
 *
 * The base descriptor is the one object whose header holds the digest of its own body, which
 * removes a bootstrap special case from every other validator.
 */
TERRAINCORE_API ETerrainPersistError TerrainPersistEncodeBaseDescriptorObject(
	const FTerrainBaseDescriptor& In,
	const FTerrainWorldId& World,
	const FTerrainStoreEpoch& Epoch,
	TArray<uint8>& OutObject,
	FTerrainDigest& OutBaseDigest);

/** Decodes and checks the self-referential BaseDigest. Mismatch is BaseMismatch. */
TERRAINCORE_API ETerrainPersistError TerrainPersistDecodeBaseDescriptorObject(
	TArrayView<const uint8> Object,
	FTerrainPersistIdentity& OutIdentity,
	FTerrainBaseDescriptor& Out);

// ---- 5. chunk payload ---------------------------------------------------

/** One differing sample. Density and material are ABSOLUTE values, not deltas. */
struct FTerrainChunkSample
{
	uint16        LocalIndex = 0;   // x + 32*y + 1024*z, < 32768
	int16         Density = 0;
	FTerrainMatId MaterialId = 0;
};

/**
 * A chunk as it is stored. Encoding is Dense or SparseDiff only.
 *
 * Empty is NOT a payload object (P-004 section 5.2): it is carried by the index leaf entry
 * alone, so one logical state never has two on-disk spellings for a validator to arbitrate.
 * The encoder refuses Empty with EncodingNotPermitted.
 */
struct FTerrainChunkPayloadRecord
{
	FTerrainChunkKey       Key;
	FTerrainRev            Rev = 0;
	FTerrainOpSeq          LastOpSeq = 0;
	ETerrainRegionEncoding Encoding = ETerrainRegionEncoding::Dense;
	uint32                 GeneratorVersion = 0;
	uint8                  ValueConfig = 0;

	/** Exactly TerrainPersistDenseBytes when Dense; empty otherwise. Transfer layout (4.2). */
	TArray<uint8>               Dense;
	/** 1..32768 strictly ascending entries when SparseDiff; empty otherwise. */
	TArray<FTerrainChunkSample> Sparse;
};

/** Builds the 131,072-byte Dense buffer from separate sample arrays. Both must be 32768 long. */
TERRAINCORE_API bool TerrainPersistBuildDense(
	TArrayView<const int16> Densities, TArrayView<const uint16> Materials, TArray<uint8>& OutDense);

/** Reads one sample out of a Dense buffer. LocalIndex must be < TerrainChunkSampleCount. */
TERRAINCORE_API int16  TerrainPersistDenseDensityAt (TArrayView<const uint8> Dense, int32 LocalIndex);
TERRAINCORE_API uint16 TerrainPersistDenseMaterialAt(TArrayView<const uint8> Dense, int32 LocalIndex);

TERRAINCORE_API ETerrainPersistError TerrainPersistEncodeChunkPayloadBody(
	const FTerrainChunkPayloadRecord& In, TArray<uint8>& OutBody);

/**
 * Decodes a chunk payload body.
 *
 * ExpectedGeneratorVersion / ExpectedValueConfig come from the base descriptor. A chunk that
 * disagrees with the base it is stored beside is BaseMismatch: P-003 section 6 requires the
 * exact base, and "this chunk was written by a different generator" is exactly that failure.
 */
TERRAINCORE_API ETerrainPersistError TerrainPersistDecodeChunkPayloadBody(
	TArrayView<const uint8> Body,
	uint32 ExpectedGeneratorVersion,
	uint8 ExpectedValueConfig,
	FTerrainChunkPayloadRecord& Out);

/**
 * P-004 section 5.3, the encoding-choice rule, implemented once.
 *
 * Compares CurrentDense against BaseDense sample by sample -- density AND material, because
 * provenance is not equality (P-003 section 6) -- and produces Empty, SparseDiff or Dense by
 * smaller encoded size with Dense on ties.
 *
 * Both buffers must be exactly TerrainPersistDenseBytes. The caller is responsible for the
 * rule this function cannot check: the samples must come from a SUCCESSFUL FULL READ of a
 * RESIDENT chunk. FMemoryTerrainBackend returns success with a default Empty for a
 * nonresident chunk and FVPLegacyBackend returns false, and neither is evidence of pristine
 * equality (P-003 section 4). A nonresident read must never reach this function.
 */
TERRAINCORE_API ETerrainPersistError TerrainPersistChooseChunkEncoding(
	const FTerrainChunkKey& Key,
	FTerrainRev Rev,
	FTerrainOpSeq LastOpSeq,
	uint32 GeneratorVersion,
	uint8 ValueConfig,
	TArrayView<const uint8> CurrentDense,
	TArrayView<const uint8> BaseDense,
	FTerrainChunkPayloadRecord& OutRecord);

// ---- 7. checkpoint descriptor -------------------------------------------

struct FTerrainCheckpointDescriptor
{
	FTerrainOpSeq  G = 0;
	uint64         Generation = 0;
	int64          CreatedUtcMillis = 0;
	/** Clear is the legal empty G=0 checkpoint published at world creation (P-003 section 4). */
	bool           bHasRootPage = false;
	uint32         RootPageLength = 0;
	FTerrainDigest RootPageDigest;
	uint64         LeafKeyCount = 0;        // advisory
	uint64         TotalPayloadBytes = 0;   // advisory
};

TERRAINCORE_API ETerrainPersistError TerrainPersistEncodeCheckpointBody(
	const FTerrainCheckpointDescriptor& In, TArray<uint8>& OutBody);

TERRAINCORE_API ETerrainPersistError TerrainPersistDecodeCheckpointBody(
	TArrayView<const uint8> Body, FTerrainCheckpointDescriptor& Out);

// ---- 8. root slot -------------------------------------------------------

struct FTerrainRootSlot
{
	uint64         Generation = 0;
	FTerrainOpSeq  G = 0;
	FTerrainDigest DescriptorDigest;
	uint32         DescriptorLength = 0;
	uint16         StoreFormatVersion = TerrainPersistSchemaVersion;
	int64          PublishedUtcMillis = 0;
};

TERRAINCORE_API ETerrainPersistError TerrainPersistEncodeRootSlotBody(
	const FTerrainRootSlot& In, TArray<uint8>& OutBody);

TERRAINCORE_API ETerrainPersistError TerrainPersistDecodeRootSlotBody(
	TArrayView<const uint8> Body, FTerrainRootSlot& Out);

// ---- 9.1 journal segment header -----------------------------------------

struct FTerrainJournalSegmentHeader
{
	uint64         SegmentId = 0;
	FTerrainOpSeq  FirstOpSeq = 0;
	uint64         PredecessorSegmentId = 0;
	FTerrainDigest PredecessorSealDigest;
	int64          CreatedUtcMillis = 0;
	bool           bHasPredecessor = false;
};

TERRAINCORE_API ETerrainPersistError TerrainPersistEncodeSegmentHeaderBody(
	const FTerrainJournalSegmentHeader& In, TArray<uint8>& OutBody);

TERRAINCORE_API ETerrainPersistError TerrainPersistDecodeSegmentHeaderBody(
	TArrayView<const uint8> Body, FTerrainJournalSegmentHeader& Out);

// ---- 9.5 journal anchor slot --------------------------------------------

struct FTerrainJournalAnchor
{
	uint64         AnchorGeneration = 0;
	uint64         ActiveSegmentId = 0;
	FTerrainOpSeq  ActiveSegmentFirstOpSeq = 0;
	FTerrainDigest PredecessorSealDigest;
	uint64         PredecessorSegmentId = 0;
	FTerrainOpSeq  PredecessorLastOpSeq = 0;
	int64          PublishedUtcMillis = 0;
	bool           bHasPredecessor = false;
};

TERRAINCORE_API ETerrainPersistError TerrainPersistEncodeAnchorBody(
	const FTerrainJournalAnchor& In, TArray<uint8>& OutBody);

TERRAINCORE_API ETerrainPersistError TerrainPersistDecodeAnchorBody(
	TArrayView<const uint8> Body, FTerrainJournalAnchor& Out);

// ---- 9.2 / 9.3 / 9.4 journal records ------------------------------------

enum class ETerrainJournalRecordType : uint8
{
	Commit = 0,
	Seal   = 1,
};

inline constexpr uint8 TerrainJournalRecordTypeMaxValue = static_cast<uint8>(ETerrainJournalRecordType::Seal);

/** P-003 section 2: the step-4 prototype writes NoEconomy and invents no yield. */
enum class ETerrainEconomyKind : uint8
{
	NoEconomy   = 0,
	ExactDeltas = 1,
};

/**
 * P-003 section 2: the prototype "must not encode unknown as a measured zero". This is that
 * rule made checkable in bytes -- Unavailable requires an empty physical list.
 */
enum class ETerrainPhysicalAvailability : uint8
{
	Unavailable = 0,
	Measured    = 1,
};

struct FTerrainChangedKeyEntry
{
	FTerrainChunkKey Key;
	FTerrainRev      BeforeRev = 0;
	FTerrainRev      AfterRev = 0;
};

/** Reserved shape for step-6 economy. EconomyKind NoEconomy requires an empty list (DEF-6). */
struct FTerrainEconomyDelta
{
	uint64 OwnerId = 0;
	uint64 ContainerId = 0;
	uint32 ItemId = 0;
	int64  Count = 0;
	uint32 Flags = 0;
};

struct FTerrainJournalCommitRecord
{
	uint64        WorldTag = 0;
	int64         ServerUtcMillis = 0;   // diagnostic only; never an ordering authority
	FTerrainOp    Op;
	/** BLAKE3 of the live admission token, truncated to 16 B. The live token is never stored. */
	uint8         TokenDigest[TerrainPersistTokenDigestBytes] = {};
	uint32        RequestId = 0;
	uint16        ChildOrdinal = 0;
	uint16        ChildCount = 1;
	FTerrainDigest IntentDigest;

	ETerrainEconomyKind          EconomyKind = ETerrainEconomyKind::NoEconomy;
	ETerrainPhysicalAvailability PhysicalAvailability = ETerrainPhysicalAvailability::Unavailable;
	uint16                       EconomyPolicyVersion = 0;

	TArray<FTerrainMaterialVolume>  Physical;
	TArray<FTerrainChangedKeyEntry> ChangedKeys;   // strictly ascending by index key, unique
	TArray<FTerrainEconomyDelta>    EconomyDeltas;
};

struct FTerrainJournalSealRecord
{
	uint64         WorldTag = 0;
	FTerrainOpSeq  LastOpSeq = 0;
	uint64         CommitRecordCount = 0;
	FTerrainDigest RecordsDigest;
	int64          SealedUtcMillis = 0;
};

/** Appends a complete framed record. Caps and field rules are checked before anything is written. */
TERRAINCORE_API ETerrainPersistError TerrainPersistEncodeCommitRecord(
	const FTerrainJournalCommitRecord& In, TArray<uint8>& OutFrame);

TERRAINCORE_API ETerrainPersistError TerrainPersistEncodeSealRecord(
	const FTerrainJournalSealRecord& In, TArray<uint8>& OutFrame);

/**
 * Reads the frame at the front of Bytes: magic, length, type, version, reserved, checksum.
 *
 * Returns the framed length in OutLength so a scanner can advance without trusting the body.
 * OutLength is also published when the frame header is structurally valid but the CHECKSUM
 * fails, because that is the only way a scanner can tell "the last region of the file is
 * damaged" -- a legal torn tail -- from "a record in the middle is damaged", which is
 * corruption. ShortBuffer means fewer bytes are present than the frame claims. Either way the
 * CALLER decides what it means; this function does not.
 */
TERRAINCORE_API ETerrainPersistError TerrainPersistPeekRecordFrame(
	TArrayView<const uint8> Bytes,
	int32& OutLength,
	ETerrainJournalRecordType& OutType);

/**
 * ExpectedWorldTag is the caller's own XXH3-64(WorldId || StoreEpoch); a mismatch is
 * WorldMismatch (P-004 section 9.2's splice check). Passing 0 skips that comparison and is
 * for callers that have no identity to check against, such as a dump of an unknown file. A
 * real world whose tag is exactly zero would therefore go unchecked; that is one value in
 * 2^64 and the splice check is a diagnostic, not the identity authority, which is the
 * segment header.
 */
TERRAINCORE_API ETerrainPersistError TerrainPersistDecodeCommitRecord(
	TArrayView<const uint8> Frame, uint64 ExpectedWorldTag, FTerrainJournalCommitRecord& Out);

TERRAINCORE_API ETerrainPersistError TerrainPersistDecodeSealRecord(
	TArrayView<const uint8> Frame, uint64 ExpectedWorldTag, FTerrainJournalSealRecord& Out);

/** P-004 section 9.3: BLAKE3 over the frame body bytes [12, Length-8). The settlement digest. */
TERRAINCORE_API ETerrainPersistError TerrainPersistComputeRecordDigest(
	TArrayView<const uint8> Frame, FTerrainDigest& OutDigest);

/**
 * P-004 section 9.6, the canonical intent digest: the 58-byte op with OpSeq ZEROED, then the
 * token digest, request ID and child identity. 82 bytes in, 32 out.
 *
 * OpSeq is zeroed because the intent exists before a sequence is assigned. It is a diagnostic
 * identity, NOT a cross-session retry key (P-003 section 1).
 */
TERRAINCORE_API FTerrainDigest TerrainPersistComputeIntentDigest(
	const FTerrainOp& Op,
	const uint8 TokenDigest[TerrainPersistTokenDigestBytes],
	uint32 RequestId,
	uint16 ChildOrdinal,
	uint16 ChildCount);

// ---- segment scanning (format validation, not the recovery driver) ------

struct FTerrainJournalScanResult
{
	FTerrainJournalSegmentHeader Header;
	int32          CommitRecordCount = 0;
	FTerrainOpSeq  FirstOpSeq = 0;
	FTerrainOpSeq  LastOpSeq = 0;
	bool           bSealed = false;
	FTerrainJournalSealRecord Seal;
	/** BLAKE3 over every commit frame in order -- what a seal record's RecordsDigest must equal. */
	FTerrainDigest RecordsDigest;
	/** Offset one past the last complete, valid record. Where a repair would truncate. */
	int32          GoodBytes = 0;
	/** Trailing bytes that form an incomplete final record. Nonzero only when bTornTail. */
	int32          TornTailBytes = 0;
	bool           bTornTail = false;
};

/**
 * Validates a whole segment image: the header object, then every framed record.
 *
 * bActiveSegment permits the one incomplete physical tail P-003 section 3 allows. On a sealed
 * or inactive segment an incomplete tail is a hard failure instead.
 *
 * Contiguity is checked here because it is a property of the bytes: each commit record's OpSeq
 * must be exactly its predecessor's plus one, and the first must equal the header's FirstOpSeq.
 * Anything after a seal record is OrderViolation.
 *
 * This reads a buffer and reports. It does not truncate, repair, open, or decide anything --
 * P-003's repair and recovery procedures are later increments and do not exist.
 */
TERRAINCORE_API ETerrainPersistError TerrainPersistScanJournalSegment(
	TArrayView<const uint8> SegmentBytes,
	const FTerrainPersistIdentity& Identity,
	bool bActiveSegment,
	FTerrainJournalScanResult& Out);

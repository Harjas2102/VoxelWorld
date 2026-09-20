// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"

/**
 * TerrainPersistenceFormat.h -- schema-2 primitives (Docs/proposals/P-004, sections 1-3).
 *
 * P-004 is the byte specification; this header and its .cpp are its implementation, and the
 * section numbers in the comments below are P-004's. ARCHITECTURE.md 4.7 adopts P-003 as the
 * architecture and names the format packet as the prerequisite for exactly this code.
 *
 * WHAT LIVES HERE: identity types, the digest and checksum choices, a bounds-checked byte
 * reader/writer, the error taxonomy, and the 96-byte object header every standalone durable
 * object carries. Record bodies are in TerrainPersistenceRecords.h; the chunk-key index is in
 * TerrainPersistenceIndex.h.
 *
 * WHAT DOES NOT LIVE HERE, DELIBERATELY: file handles, paths, SQLite, the commit path and the
 * checkpoint pump. Nothing in this header includes anything outside Core, so the whole format
 * compiles and tests headless with no engine world, no plugin and no file system (6.1). A
 * later increment adds the storage owner behind a seam; it does not get to reach back in.
 *
 * THE RULES THIS FILE ENFORCES, from P-004 section 1:
 *   - explicit little-endian, field by field, never a struct memcpy and never sizeof;
 *   - no floating point is persisted anywhere in schema 2;
 *   - reserved bytes are zero on write and MUST be zero on read;
 *   - validation is ordered and fail-closed, and a decoder never half-writes its output;
 *   - a decode that does not consume exactly the declared length is TrailingBytes.
 */

// ---- constants (P-004 section 10) --------------------------------------
inline constexpr uint16 TerrainPersistSchemaVersion   = 2;
inline constexpr int32  TerrainPersistObjectHeaderSize = 96;
inline constexpr int32  TerrainPersistSlotSize         = 4096;
inline constexpr int32  TerrainPersistSlotBodySize     = TerrainPersistSlotSize - TerrainPersistObjectHeaderSize; // 4000
inline constexpr int32  TerrainPersistDigestSize       = 32;
inline constexpr int32  TerrainPersistIdBytes          = 16;

/** Schema 1 is the withdrawn legacy sketch (P-003 section 7). It is never emitted. */
inline constexpr uint16 TerrainPersistLegacySchemaVersion = 1;

/**
 * P-004 section 10, "Caps, collected" -- all of them, in one place, because a cap that is
 * written down twice is a cap that will eventually disagree with itself.
 */
inline constexpr int32 TerrainPersistMaxBaseDescriptorBody       = 8192;
inline constexpr int32 TerrainPersistMaxChunkPayloadBody         = 131104;
inline constexpr int32 TerrainPersistMaxIndexPageBody            = 32768;
inline constexpr int32 TerrainPersistMaxCheckpointDescriptorBody = 16384;
inline constexpr int32 TerrainPersistMaxJournalSegmentHeaderBody = 72;
inline constexpr int32 TerrainPersistMaxIndexEntriesPerPage      = 256;
inline constexpr int32 TerrainPersistIndexKeyBytes               = 12;
inline constexpr int32 TerrainPersistIndexMaxDepth               = 12;
inline constexpr int32 TerrainPersistMaxJournalRecordBytes       = 131072;
inline constexpr int32 TerrainPersistMaxChangedKeysPerRecord     = 4096;
inline constexpr int32 TerrainPersistMaxPhysicalEntriesPerRecord = 64;
inline constexpr int32 TerrainPersistMaxEconomyDeltasPerRecord   = 64;
inline constexpr int32 TerrainPersistMaxSparseSamples            = 32768;
inline constexpr int32 TerrainPersistMaxNameBytes                = 128;
inline constexpr int32 TerrainPersistMaxAuthoredStamps           = 1024;

/**
 * Why the decoder returns a code and not a bool.
 *
 * The corrupt-fixture test asserts WHICH defence fired. A bool would let a fixture that was
 * meant to prove the ordering check works instead pass because the length check rejected it
 * first -- which is how a format grows a defence that has never actually run.
 */
enum class ETerrainPersistError : uint8
{
	None = 0,
	ShortBuffer,
	BadMagic,
	UnsupportedSchema,
	BadHeaderSize,
	UnknownObjectType,
	HeaderChecksumMismatch,
	BodyChecksumMismatch,
	BodyLengthOutOfRange,
	WorldMismatch,
	EpochMismatch,
	BaseMismatch,
	TrailingBytes,
	FieldOutOfRange,
	ReservedNotZero,
	OrderViolation,
	DuplicateKey,
	EncodingNotPermitted,
	CapExceeded,
	TornTail,
};

TERRAINCORE_API const TCHAR* TerrainPersistErrorName(ETerrainPersistError Error);

// ---- identity (P-004 section 2) ----------------------------------------

/**
 * BLAKE3-256, full width. Content identity, never a location.
 *
 * BLAKE3 and XXH3 are specified algorithms vendored in Core. FCrc::MemCrc32 is not: its value
 * is an Unreal implementation detail, and writing one into a file that has to outlive an
 * engine upgrade would make every saved world hostage to a header Epic is free to change.
 */
struct FTerrainDigest
{
	uint8 Bytes[TerrainPersistDigestSize] = {};

	bool IsZero() const;

	friend bool operator==(const FTerrainDigest& A, const FTerrainDigest& B)
	{
		return FMemory::Memcmp(A.Bytes, B.Bytes, TerrainPersistDigestSize) == 0;
	}
	friend bool operator!=(const FTerrainDigest& A, const FTerrainDigest& B) { return !(A == B); }

	friend uint32 GetTypeHash(const FTerrainDigest& Digest)
	{
		uint32 Hash = 0;
		FMemory::Memcpy(&Hash, Digest.Bytes, sizeof(uint32));
		return Hash;
	}
};

/**
 * 128-bit opaque identities. NOT FGuid: FGuid's four-uint32 layout and its several string
 * forms are an engine convention with a byte-order question attached, and an opaque 16-byte
 * array has none. WorldId is never inferred from a directory name (P-003 section 1).
 */
struct FTerrainWorldId
{
	uint8 Bytes[TerrainPersistIdBytes] = {};

	bool IsZero() const;

	friend bool operator==(const FTerrainWorldId& A, const FTerrainWorldId& B)
	{
		return FMemory::Memcmp(A.Bytes, B.Bytes, TerrainPersistIdBytes) == 0;
	}
	friend bool operator!=(const FTerrainWorldId& A, const FTerrainWorldId& B) { return !(A == B); }
};

struct FTerrainStoreEpoch
{
	uint8 Bytes[TerrainPersistIdBytes] = {};

	bool IsZero() const;

	friend bool operator==(const FTerrainStoreEpoch& A, const FTerrainStoreEpoch& B)
	{
		return FMemory::Memcmp(A.Bytes, B.Bytes, TerrainPersistIdBytes) == 0;
	}
	friend bool operator!=(const FTerrainStoreEpoch& A, const FTerrainStoreEpoch& B) { return !(A == B); }
};

/** What every object header binds itself to: world, store lineage, and the exact world shape. */
struct FTerrainPersistIdentity
{
	FTerrainWorldId    World;
	FTerrainStoreEpoch Epoch;
	FTerrainDigest     BaseDigest;   // BLAKE3 of the base-descriptor BODY (P-004 section 4)

	friend bool operator==(const FTerrainPersistIdentity& A, const FTerrainPersistIdentity& B)
	{
		return A.World == B.World && A.Epoch == B.Epoch && A.BaseDigest == B.BaseDigest;
	}
	friend bool operator!=(const FTerrainPersistIdentity& A, const FTerrainPersistIdentity& B) { return !(A == B); }
};

// ---- hashing ------------------------------------------------------------
TERRAINCORE_API FTerrainDigest TerrainPersistDigest(TArrayView<const uint8> Bytes);
TERRAINCORE_API uint64         TerrainPersistChecksum(TArrayView<const uint8> Bytes);

/** WorldTag = XXH3-64(WorldId || StoreEpoch). The journal record splice check (P-004 2, 9.2). */
TERRAINCORE_API uint64 TerrainPersistWorldTag(const FTerrainWorldId& World, const FTerrainStoreEpoch& Epoch);

/** Exactly 64 lowercase hex characters. This is the only rule by which an object is named. */
TERRAINCORE_API FString TerrainPersistDigestToHex(const FTerrainDigest& Digest);

/**
 * Parses exactly 64 lowercase hex characters. Anything else -- wrong length, uppercase, a
 * path separator, a dot -- returns false and leaves OutDigest untouched.
 *
 * This is the check P-004 section 11.1 requires BEFORE an object ID becomes part of a path:
 * "there is no code path in which a byte from a file becomes a path component without it".
 */
TERRAINCORE_API bool TerrainPersistDigestFromHex(const FString& Hex, FTerrainDigest& OutDigest);

// ---- bounded byte cursors ----------------------------------------------

/**
 * Append-only little-endian writer. Every Write* appends; nothing seeks, so a body cannot be
 * written with a hole in it, and the encoders assert their own measured length afterwards.
 */
class TERRAINCORE_API FTerrainByteWriter
{
public:
	explicit FTerrainByteWriter(TArray<uint8>& InOut) : Out(InOut), StartNum(InOut.Num()) {}

	void WriteU8 (uint8  Value);
	void WriteU16(uint16 Value);
	void WriteU32(uint32 Value);
	void WriteU64(uint64 Value);
	void WriteI16(int16  Value) { WriteU16(static_cast<uint16>(Value)); }
	void WriteI32(int32  Value) { WriteU32(static_cast<uint32>(Value)); }
	void WriteI64(int64  Value) { WriteU64(static_cast<uint64>(Value)); }
	void WriteBytes(const uint8* Data, int32 Count);
	void WriteBytes(TArrayView<const uint8> Bytes) { WriteBytes(Bytes.GetData(), Bytes.Num()); }
	void WriteDigest(const FTerrainDigest& Digest) { WriteBytes(Digest.Bytes, TerrainPersistDigestSize); }
	void WriteWorldId(const FTerrainWorldId& Id) { WriteBytes(Id.Bytes, TerrainPersistIdBytes); }
	void WriteEpoch(const FTerrainStoreEpoch& Id) { WriteBytes(Id.Bytes, TerrainPersistIdBytes); }
	/** Reserved bytes. Always zero on write (P-004 section 1 rule 5). */
	void WriteZero(int32 Count);

	/** Bytes appended by this writer so far. The encoders check this against P-004's tables. */
	int32 BytesWritten() const { return Out.Num() - StartNum; }

private:
	TArray<uint8>& Out;
	int32 StartNum;
};

/**
 * Bounds-checked little-endian reader.
 *
 * Every read checks the remaining length first. Once a read has failed the cursor latches
 * into a failed state and every later read returns zero without touching memory, so a decoder
 * may read a whole body and check the cursor once at the end without ever having read out of
 * bounds on the way. Reads never throw and never assert on malformed input: this reader's
 * whole input is untrusted bytes from disk.
 */
class TERRAINCORE_API FTerrainByteReader
{
public:
	explicit FTerrainByteReader(TArrayView<const uint8> InBytes) : Bytes(InBytes) {}

	uint8  ReadU8();
	uint16 ReadU16();
	uint32 ReadU32();
	uint64 ReadU64();
	int16  ReadI16() { return static_cast<int16 >(ReadU16()); }
	int32  ReadI32() { return static_cast<int32 >(ReadU32()); }
	int64  ReadI64() { return static_cast<int64 >(ReadU64()); }
	void   ReadBytes(uint8* OutData, int32 Count);
	void   ReadDigest(FTerrainDigest& Out) { ReadBytes(Out.Bytes, TerrainPersistDigestSize); }
	void   ReadWorldId(FTerrainWorldId& Out) { ReadBytes(Out.Bytes, TerrainPersistIdBytes); }
	void   ReadEpoch(FTerrainStoreEpoch& Out) { ReadBytes(Out.Bytes, TerrainPersistIdBytes); }

	/** Reads Count reserved bytes and fails with ReservedNotZero unless every one is zero. */
	bool ReadZero(int32 Count);

	/** A view of the next Count bytes without copying; empty and fails if they are not there. */
	TArrayView<const uint8> ReadView(int32 Count);

	/** Skips Count bytes; fails if they are not there. */
	void Skip(int32 Count);

	bool  IsValid() const { return bOk; }
	int32 Tell() const { return Cursor; }
	int32 Remaining() const { return bOk ? Bytes.Num() - Cursor : 0; }
	bool  AtEnd() const { return bOk && Cursor == Bytes.Num(); }

	/** Latches the failed state. Used by decoders for their own semantic failures. */
	void Fail() { bOk = false; }

	/** The reason the reader itself failed: ShortBuffer, ReservedNotZero, or None. */
	ETerrainPersistError Error() const { return LatchedError; }

private:
	bool Require(int32 Count);

	TArrayView<const uint8> Bytes;
	int32 Cursor = 0;
	bool  bOk = true;
	ETerrainPersistError LatchedError = ETerrainPersistError::None;
};

// ---- the object header (P-004 section 3) -------------------------------

enum class ETerrainPersistObjectType : uint8
{
	BaseDescriptor       = 1,
	ChunkPayload         = 2,
	IndexPage            = 3,
	CheckpointDescriptor = 4,
	RootSlot             = 5,
	JournalSegmentHeader = 6,
	JournalAnchorSlot    = 7,
};

inline constexpr uint8 TerrainPersistObjectTypeMaxValue =
	static_cast<uint8>(ETerrainPersistObjectType::JournalAnchorSlot);

/** What a decoded header carries. BodyLength and BodyChecksum are the values read from disk. */
struct FTerrainPersistObjectHeader
{
	ETerrainPersistObjectType ObjectType = ETerrainPersistObjectType::BaseDescriptor;
	FTerrainPersistIdentity   Identity;
	uint64                    BodyLength   = 0;
	uint64                    BodyChecksum = 0;
};

/**
 * Appends a complete object -- 96-byte header then Body -- to OutBytes.
 *
 * The caller has already produced Body with one of the record encoders. This function is the
 * only place a schema-2 object header is written.
 *
 * Returns CapExceeded, and appends NOTHING, when Body is larger than schema 2 permits for
 * that type. This is a returned error rather than a check(): checks compile out of a shipping
 * build, and an over-cap object written by a shipping server would be one that no decoder --
 * including its own -- will ever accept again.
 */
TERRAINCORE_API ETerrainPersistError TerrainPersistEncodeObject(
	ETerrainPersistObjectType Type,
	const FTerrainPersistIdentity& Identity,
	TArrayView<const uint8> Body,
	TArray<uint8>& OutBytes);

/**
 * Validates an object in P-004 section 1 rule 8's order and hands back a VIEW of its body.
 *
 * ExpectedType is always checked; a well-formed header of the wrong type is UnknownObjectType,
 * so a root slot can never be decoded as a checkpoint descriptor.
 *
 * ExpectedIdentity may be null, which skips the world/epoch/base comparison. The one caller
 * that legitimately passes null is the base-descriptor decoder, whose BaseDigest is the digest
 * of its own body and is therefore checked against the body rather than against a caller's
 * expectation. Every other caller passes the store's identity: P-003 section 5 requires that
 * copying a file between worlds is rejected, and this is where that happens.
 *
 * OutBody aliases Bytes and is only valid while Bytes is.
 */
TERRAINCORE_API ETerrainPersistError TerrainPersistDecodeObject(
	TArrayView<const uint8> Bytes,
	ETerrainPersistObjectType ExpectedType,
	const FTerrainPersistIdentity* ExpectedIdentity,
	FTerrainPersistObjectHeader& OutHeader,
	TArrayView<const uint8>& OutBody);

/**
 * As TerrainPersistDecodeObject, but for an object that is a PREFIX of a longer file, and
 * reports how many bytes it consumed.
 *
 * The one thing schema 2 stores this way is a journal segment: its 96-byte header object is
 * followed by framed records in the same file (P-004 section 11.1). Every other object stands
 * alone and must consume its buffer exactly, which is why that is the default and this is the
 * named exception rather than a flag on the common path.
 */
TERRAINCORE_API ETerrainPersistError TerrainPersistDecodeObjectPrefix(
	TArrayView<const uint8> Bytes,
	ETerrainPersistObjectType ExpectedType,
	const FTerrainPersistIdentity* ExpectedIdentity,
	FTerrainPersistObjectHeader& OutHeader,
	TArrayView<const uint8>& OutBody,
	int32& OutConsumed);

/**
 * The largest body schema 2 permits for a given type. A header claiming more is
 * BodyLengthOutOfRange and is refused BEFORE the body is hashed or copied -- which is the
 * reason the header carries its own checksum separately from the body's.
 */
TERRAINCORE_API uint64 TerrainPersistMaxBodyLength(ETerrainPersistObjectType Type);

/**
 * Pads a 4000-byte slot body to a complete 4096-byte slot file image (P-004 sections 8, 9.5).
 *
 * The body is already 4000 bytes including its reserved tail, so the whole slot is covered by
 * the one body checksum. Returns false if Body is not exactly TerrainPersistSlotBodySize.
 */
TERRAINCORE_API bool TerrainPersistEncodeSlot(
	ETerrainPersistObjectType Type,
	const FTerrainPersistIdentity& Identity,
	TArrayView<const uint8> Body,
	TArray<uint8>& OutSlot);

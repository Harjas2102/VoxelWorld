// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainPersistenceFormat.h"

#include "Hash/Blake3.h"
#include "Hash/xxhash.h"

/**
 * Schema-2 primitives (Docs/proposals/P-004 sections 1-3).
 *
 * PERMANENT FORMAT. Everything below is what a saved world means. A change to any offset,
 * width or order here changes what an existing save decodes as, and under AGENTS.md section 4
 * that is a save-format change with a migration path, not an edit to this file.
 */

namespace
{
	/** ASCII "VXTP". Written byte by byte so the constant cannot acquire an endianness. */
	constexpr uint8 ObjectMagic[4] = { 0x56, 0x58, 0x54, 0x50 };

	constexpr int32 HeaderChecksumOffset = 88;   // header bytes [0,88) are what it covers
}

const TCHAR* TerrainPersistErrorName(ETerrainPersistError Error)
{
	switch (Error)
	{
	case ETerrainPersistError::None:                   return TEXT("None");
	case ETerrainPersistError::ShortBuffer:            return TEXT("ShortBuffer");
	case ETerrainPersistError::BadMagic:               return TEXT("BadMagic");
	case ETerrainPersistError::UnsupportedSchema:      return TEXT("UnsupportedSchema");
	case ETerrainPersistError::BadHeaderSize:          return TEXT("BadHeaderSize");
	case ETerrainPersistError::UnknownObjectType:      return TEXT("UnknownObjectType");
	case ETerrainPersistError::HeaderChecksumMismatch: return TEXT("HeaderChecksumMismatch");
	case ETerrainPersistError::BodyChecksumMismatch:   return TEXT("BodyChecksumMismatch");
	case ETerrainPersistError::BodyLengthOutOfRange:   return TEXT("BodyLengthOutOfRange");
	case ETerrainPersistError::WorldMismatch:          return TEXT("WorldMismatch");
	case ETerrainPersistError::EpochMismatch:          return TEXT("EpochMismatch");
	case ETerrainPersistError::BaseMismatch:           return TEXT("BaseMismatch");
	case ETerrainPersistError::TrailingBytes:          return TEXT("TrailingBytes");
	case ETerrainPersistError::FieldOutOfRange:        return TEXT("FieldOutOfRange");
	case ETerrainPersistError::ReservedNotZero:        return TEXT("ReservedNotZero");
	case ETerrainPersistError::OrderViolation:         return TEXT("OrderViolation");
	case ETerrainPersistError::DuplicateKey:           return TEXT("DuplicateKey");
	case ETerrainPersistError::EncodingNotPermitted:   return TEXT("EncodingNotPermitted");
	case ETerrainPersistError::CapExceeded:            return TEXT("CapExceeded");
	case ETerrainPersistError::TornTail:               return TEXT("TornTail");
	}
	return TEXT("Unknown");
}

// ---- identity -----------------------------------------------------------

bool FTerrainDigest::IsZero() const
{
	for (int32 Index = 0; Index < TerrainPersistDigestSize; ++Index)
	{
		if (Bytes[Index] != 0) { return false; }
	}
	return true;
}

bool FTerrainWorldId::IsZero() const
{
	for (int32 Index = 0; Index < TerrainPersistIdBytes; ++Index)
	{
		if (Bytes[Index] != 0) { return false; }
	}
	return true;
}

bool FTerrainStoreEpoch::IsZero() const
{
	for (int32 Index = 0; Index < TerrainPersistIdBytes; ++Index)
	{
		if (Bytes[Index] != 0) { return false; }
	}
	return true;
}

// ---- hashing ------------------------------------------------------------

FTerrainDigest TerrainPersistDigest(TArrayView<const uint8> Bytes)
{
	const FBlake3Hash Hash = FBlake3::HashBuffer(Bytes.GetData(), static_cast<uint64>(Bytes.Num()));

	FTerrainDigest Out;
	FMemory::Memcpy(Out.Bytes, Hash.GetBytes(), TerrainPersistDigestSize);
	return Out;
}

uint64 TerrainPersistChecksum(TArrayView<const uint8> Bytes)
{
	return FXxHash64::HashBuffer(Bytes.GetData(), static_cast<uint64>(Bytes.Num())).Hash;
}

uint64 TerrainPersistWorldTag(const FTerrainWorldId& World, const FTerrainStoreEpoch& Epoch)
{
	uint8 Combined[TerrainPersistIdBytes * 2];
	FMemory::Memcpy(Combined, World.Bytes, TerrainPersistIdBytes);
	FMemory::Memcpy(Combined + TerrainPersistIdBytes, Epoch.Bytes, TerrainPersistIdBytes);
	return TerrainPersistChecksum(TArrayView<const uint8>(Combined, UE_ARRAY_COUNT(Combined)));
}

FString TerrainPersistDigestToHex(const FTerrainDigest& Digest)
{
	static const TCHAR* Nibbles = TEXT("0123456789abcdef");

	FString Hex;
	Hex.Reserve(TerrainPersistDigestSize * 2);
	for (int32 Index = 0; Index < TerrainPersistDigestSize; ++Index)
	{
		Hex.AppendChar(Nibbles[(Digest.Bytes[Index] >> 4) & 0x0F]);
		Hex.AppendChar(Nibbles[ Digest.Bytes[Index]       & 0x0F]);
	}
	return Hex;
}

bool TerrainPersistDigestFromHex(const FString& Hex, FTerrainDigest& OutDigest)
{
	// Exact length first: this is the check that stops a path separator, a "..", a dot or a
	// drive letter from ever reaching a file name (P-004 section 11.1).
	if (Hex.Len() != TerrainPersistDigestSize * 2)
	{
		return false;
	}

	FTerrainDigest Decoded;
	for (int32 Index = 0; Index < TerrainPersistDigestSize; ++Index)
	{
		uint8 Value = 0;
		for (int32 Half = 0; Half < 2; ++Half)
		{
			const TCHAR Char = Hex[Index * 2 + Half];
			uint8 Nibble;
			if      (Char >= TEXT('0') && Char <= TEXT('9')) { Nibble = static_cast<uint8>(Char - TEXT('0')); }
			else if (Char >= TEXT('a') && Char <= TEXT('f')) { Nibble = static_cast<uint8>(Char - TEXT('a') + 10); }
			else { return false; }   // uppercase included: the naming rule is lowercase only

			Value = static_cast<uint8>((Value << 4) | Nibble);
		}
		Decoded.Bytes[Index] = Value;
	}

	OutDigest = Decoded;
	return true;
}

// ---- writer -------------------------------------------------------------

void FTerrainByteWriter::WriteU8(uint8 Value)
{
	Out.Add(Value);
}

void FTerrainByteWriter::WriteU16(uint16 Value)
{
	Out.Add(static_cast<uint8>( Value       & 0xFFu));
	Out.Add(static_cast<uint8>((Value >> 8) & 0xFFu));
}

void FTerrainByteWriter::WriteU32(uint32 Value)
{
	for (int32 Shift = 0; Shift < 32; Shift += 8)
	{
		Out.Add(static_cast<uint8>((Value >> Shift) & 0xFFu));
	}
}

void FTerrainByteWriter::WriteU64(uint64 Value)
{
	for (int32 Shift = 0; Shift < 64; Shift += 8)
	{
		Out.Add(static_cast<uint8>((Value >> Shift) & 0xFFull));
	}
}

void FTerrainByteWriter::WriteBytes(const uint8* Data, int32 Count)
{
	if (Count > 0)
	{
		Out.Append(Data, Count);
	}
}

void FTerrainByteWriter::WriteZero(int32 Count)
{
	Out.AddZeroed(Count);
}

// ---- reader -------------------------------------------------------------

bool FTerrainByteReader::Require(int32 Count)
{
	if (!bOk)
	{
		return false;
	}
	if (Count < 0 || Bytes.Num() - Cursor < Count)
	{
		bOk = false;
		LatchedError = ETerrainPersistError::ShortBuffer;
		return false;
	}
	return true;
}

uint8 FTerrainByteReader::ReadU8()
{
	if (!Require(1)) { return 0; }
	return Bytes[Cursor++];
}

uint16 FTerrainByteReader::ReadU16()
{
	if (!Require(2)) { return 0; }
	const uint16 Value = static_cast<uint16>(
		  static_cast<uint16>(Bytes[Cursor])
		| static_cast<uint16>(static_cast<uint16>(Bytes[Cursor + 1]) << 8));
	Cursor += 2;
	return Value;
}

uint32 FTerrainByteReader::ReadU32()
{
	if (!Require(4)) { return 0; }
	uint32 Value = 0;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Value |= static_cast<uint32>(Bytes[Cursor + Index]) << (Index * 8);
	}
	Cursor += 4;
	return Value;
}

uint64 FTerrainByteReader::ReadU64()
{
	if (!Require(8)) { return 0; }
	uint64 Value = 0;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Value |= static_cast<uint64>(Bytes[Cursor + Index]) << (Index * 8);
	}
	Cursor += 8;
	return Value;
}

void FTerrainByteReader::ReadBytes(uint8* OutData, int32 Count)
{
	if (!Require(Count))
	{
		// Zero the caller's buffer rather than leave it undefined: a decoder that ignores the
		// failed state must still not be handed uninitialised stack bytes.
		FMemory::Memzero(OutData, Count > 0 ? Count : 0);
		return;
	}
	FMemory::Memcpy(OutData, Bytes.GetData() + Cursor, Count);
	Cursor += Count;
}

bool FTerrainByteReader::ReadZero(int32 Count)
{
	if (!Require(Count)) { return false; }

	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (Bytes[Cursor + Index] != 0)
		{
			// P-004 section 1 rule 5: reserved is not a forward-compatibility escape hatch.
			bOk = false;
			LatchedError = ETerrainPersistError::ReservedNotZero;
			return false;
		}
	}
	Cursor += Count;
	return true;
}

TArrayView<const uint8> FTerrainByteReader::ReadView(int32 Count)
{
	if (!Require(Count)) { return TArrayView<const uint8>(); }

	TArrayView<const uint8> View(Bytes.GetData() + Cursor, Count);
	Cursor += Count;
	return View;
}

void FTerrainByteReader::Skip(int32 Count)
{
	if (Require(Count))
	{
		Cursor += Count;
	}
}

// ---- object header ------------------------------------------------------

uint64 TerrainPersistMaxBodyLength(ETerrainPersistObjectType Type)
{
	switch (Type)
	{
	case ETerrainPersistObjectType::BaseDescriptor:       return TerrainPersistMaxBaseDescriptorBody;
	case ETerrainPersistObjectType::ChunkPayload:         return TerrainPersistMaxChunkPayloadBody;
	case ETerrainPersistObjectType::IndexPage:            return TerrainPersistMaxIndexPageBody;
	case ETerrainPersistObjectType::CheckpointDescriptor: return TerrainPersistMaxCheckpointDescriptorBody;
	case ETerrainPersistObjectType::RootSlot:             return TerrainPersistSlotBodySize;
	case ETerrainPersistObjectType::JournalSegmentHeader: return TerrainPersistMaxJournalSegmentHeaderBody;
	case ETerrainPersistObjectType::JournalAnchorSlot:    return TerrainPersistSlotBodySize;
	}
	return 0;
}

ETerrainPersistError TerrainPersistEncodeObject(
	ETerrainPersistObjectType Type,
	const FTerrainPersistIdentity& Identity,
	TArrayView<const uint8> Body,
	TArray<uint8>& OutBytes)
{
	// Enforced in every build configuration, not by a check(). Nothing is appended on failure.
	if (Body.Num() < 0 || static_cast<uint64>(Body.Num()) > TerrainPersistMaxBodyLength(Type))
	{
		return ETerrainPersistError::CapExceeded;
	}

	const int32 StartNum = OutBytes.Num();
	OutBytes.Reserve(StartNum + TerrainPersistObjectHeaderSize + Body.Num());

	FTerrainByteWriter Writer(OutBytes);

	Writer.WriteBytes(ObjectMagic, 4);                                   //  0 ..  3
	Writer.WriteU16(TerrainPersistSchemaVersion);                        //  4 ..  5
	Writer.WriteU8(static_cast<uint8>(Type));                            //  6
	Writer.WriteU8(static_cast<uint8>(TerrainPersistObjectHeaderSize));  //  7
	Writer.WriteWorldId(Identity.World);                                 //  8 .. 23
	Writer.WriteEpoch(Identity.Epoch);                                   // 24 .. 39
	Writer.WriteDigest(Identity.BaseDigest);                             // 40 .. 71
	Writer.WriteU64(static_cast<uint64>(Body.Num()));                    // 72 .. 79
	Writer.WriteU64(TerrainPersistChecksum(Body));                       // 80 .. 87

	check(Writer.BytesWritten() == HeaderChecksumOffset);

	// The header checksum covers [0,88) of THIS header, so a reader can trust BodyLength
	// before it allocates or hashes BodyLength bytes.
	const uint64 HeaderChecksum = TerrainPersistChecksum(
		TArrayView<const uint8>(OutBytes.GetData() + StartNum, HeaderChecksumOffset));
	Writer.WriteU64(HeaderChecksum);                                     // 88 .. 95

	check(Writer.BytesWritten() == TerrainPersistObjectHeaderSize);

	OutBytes.Append(Body.GetData(), Body.Num());

	checkf(OutBytes.Num() - StartNum == TerrainPersistObjectHeaderSize + Body.Num(),
		TEXT("Schema-2 object encoded to an unexpected length."));
	return ETerrainPersistError::None;
}

ETerrainPersistError TerrainPersistDecodeObject(
	TArrayView<const uint8> Bytes,
	ETerrainPersistObjectType ExpectedType,
	const FTerrainPersistIdentity* ExpectedIdentity,
	FTerrainPersistObjectHeader& OutHeader,
	TArrayView<const uint8>& OutBody)
{
	int32 Consumed = 0;
	const ETerrainPersistError Error = TerrainPersistDecodeObjectPrefix(
		Bytes, ExpectedType, ExpectedIdentity, OutHeader, OutBody, Consumed);
	if (Error != ETerrainPersistError::None)
	{
		return Error;
	}

	// P-004 section 1 rule 9. A standalone object file is exactly its header plus its body.
	if (Consumed != Bytes.Num())
	{
		return ETerrainPersistError::TrailingBytes;
	}
	return ETerrainPersistError::None;
}

ETerrainPersistError TerrainPersistDecodeObjectPrefix(
	TArrayView<const uint8> Bytes,
	ETerrainPersistObjectType ExpectedType,
	const FTerrainPersistIdentity* ExpectedIdentity,
	FTerrainPersistObjectHeader& OutHeader,
	TArrayView<const uint8>& OutBody,
	int32& OutConsumed)
{
	// P-004 section 1 rule 8, in order. Nothing below this line writes to OutHeader or OutBody
	// until every check has passed.
	if (Bytes.Num() < TerrainPersistObjectHeaderSize)
	{
		return ETerrainPersistError::ShortBuffer;
	}

	FTerrainByteReader Reader(Bytes);

	uint8 Magic[4];
	Reader.ReadBytes(Magic, 4);
	if (FMemory::Memcmp(Magic, ObjectMagic, 4) != 0)
	{
		return ETerrainPersistError::BadMagic;
	}

	const uint16 Schema = Reader.ReadU16();
	if (Schema != TerrainPersistSchemaVersion)
	{
		// Schema 1 is the withdrawn sketch and has no registered decoder (P-003 section 7).
		return ETerrainPersistError::UnsupportedSchema;
	}

	const uint8 TypeByte   = Reader.ReadU8();
	const uint8 HeaderSize = Reader.ReadU8();
	if (HeaderSize != TerrainPersistObjectHeaderSize)
	{
		return ETerrainPersistError::BadHeaderSize;
	}

	// Header integrity BEFORE the type and the length are believed. The stored value sits at a
	// fixed offset, so it is read with its own cursor rather than by seeking this one.
	FTerrainByteReader ChecksumField(TArrayView<const uint8>(Bytes.GetData() + HeaderChecksumOffset, 8));
	const uint64 StoredHeaderChecksum = ChecksumField.ReadU64();

	const uint64 ComputedHeaderChecksum = TerrainPersistChecksum(
		TArrayView<const uint8>(Bytes.GetData(), HeaderChecksumOffset));
	if (StoredHeaderChecksum != ComputedHeaderChecksum)
	{
		return ETerrainPersistError::HeaderChecksumMismatch;
	}

	if (TypeByte == 0 || TypeByte > TerrainPersistObjectTypeMaxValue)
	{
		return ETerrainPersistError::UnknownObjectType;
	}
	const ETerrainPersistObjectType Type = static_cast<ETerrainPersistObjectType>(TypeByte);
	if (Type != ExpectedType)
	{
		// A well-formed header of the wrong type. A root slot must never be read as a
		// checkpoint descriptor just because both validate structurally.
		return ETerrainPersistError::UnknownObjectType;
	}

	FTerrainPersistIdentity Identity;
	Reader.ReadWorldId(Identity.World);
	Reader.ReadEpoch(Identity.Epoch);
	Reader.ReadDigest(Identity.BaseDigest);

	if (ExpectedIdentity != nullptr)
	{
		// P-003 section 5: copying a file across worlds or store lineages is rejected here.
		if (Identity.World != ExpectedIdentity->World)       { return ETerrainPersistError::WorldMismatch; }
		if (Identity.Epoch != ExpectedIdentity->Epoch)       { return ETerrainPersistError::EpochMismatch; }
		if (Identity.BaseDigest != ExpectedIdentity->BaseDigest) { return ETerrainPersistError::BaseMismatch; }
	}

	const uint64 BodyLength   = Reader.ReadU64();
	const uint64 BodyChecksum = Reader.ReadU64();

	check(Reader.IsValid());   // 96 bytes were verified present above

	if (BodyLength > TerrainPersistMaxBodyLength(Type))
	{
		return ETerrainPersistError::BodyLengthOutOfRange;
	}

	const int64 Available = static_cast<int64>(Bytes.Num()) - TerrainPersistObjectHeaderSize;
	if (static_cast<int64>(BodyLength) > Available)
	{
		return ETerrainPersistError::ShortBuffer;
	}

	const TArrayView<const uint8> Body(
		Bytes.GetData() + TerrainPersistObjectHeaderSize, static_cast<int32>(BodyLength));

	if (TerrainPersistChecksum(Body) != BodyChecksum)
	{
		return ETerrainPersistError::BodyChecksumMismatch;
	}

	OutHeader.ObjectType   = Type;
	OutHeader.Identity     = Identity;
	OutHeader.BodyLength   = BodyLength;
	OutHeader.BodyChecksum = BodyChecksum;
	OutBody                = Body;
	OutConsumed            = TerrainPersistObjectHeaderSize + static_cast<int32>(BodyLength);
	return ETerrainPersistError::None;
}

bool TerrainPersistEncodeSlot(
	ETerrainPersistObjectType Type,
	const FTerrainPersistIdentity& Identity,
	TArrayView<const uint8> Body,
	TArray<uint8>& OutSlot)
{
	if (Type != ETerrainPersistObjectType::RootSlot && Type != ETerrainPersistObjectType::JournalAnchorSlot)
	{
		return false;
	}
	if (Body.Num() != TerrainPersistSlotBodySize)
	{
		return false;
	}

	OutSlot.Reset();
	if (TerrainPersistEncodeObject(Type, Identity, Body, OutSlot) != ETerrainPersistError::None)
	{
		return false;
	}

	// The body already carries its own reserved tail, so the single body checksum covers the
	// whole 4096-byte slot: P-003's "whole-slot checksum" without a second checksum field.
	check(OutSlot.Num() == TerrainPersistSlotSize);
	return true;
}

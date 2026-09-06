// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainOp.h"

/**
 * The FTerrainOp codec (ARCHITECTURE.md §4.2).
 *
 * PERMANENT FORMAT. This is the wire format and the journal record body at once, so the byte
 * layout below is a persistence format under AGENTS.md §4 — versioned, flat and inspectable.
 * Changing it changes what old saves mean and requires a numbered decision.
 *
 * Rules, all of them deliberate:
 *   - Explicit little-endian, byte by byte. No htonl, no FMemory::Memcpy of the struct, no
 *     sizeof(FTerrainOp) anywhere in this file. In-memory size and padding are irrelevant to
 *     the format, and a compiler that repacks the struct must not be able to change a save.
 *   - Declaration order, matching §4.2 exactly.
 *   - Enums cross as their uint8 value and are range-checked on the way in. A value this
 *     build does not define is a decode failure, not a silent cast — a future build's op
 *     kind must not be read as Remove.
 *   - Signed integers are round-tripped through their unsigned counterpart. The cast back is
 *     implementation-defined before C++20 in theory and two's-complement everywhere in
 *     practice; static_cast from the unsigned type is well-defined in C++20, which is what
 *     UE 5.7 compiles as.
 */

namespace
{
	void WriteU8(TArray<uint8>& Out, uint8 Value)
	{
		Out.Add(Value);
	}

	void WriteU16LE(TArray<uint8>& Out, uint16 Value)
	{
		Out.Add(static_cast<uint8>( Value        & 0xFFu));
		Out.Add(static_cast<uint8>((Value >> 8)  & 0xFFu));
	}

	void WriteU32LE(TArray<uint8>& Out, uint32 Value)
	{
		Out.Add(static_cast<uint8>( Value        & 0xFFu));
		Out.Add(static_cast<uint8>((Value >>  8) & 0xFFu));
		Out.Add(static_cast<uint8>((Value >> 16) & 0xFFu));
		Out.Add(static_cast<uint8>((Value >> 24) & 0xFFu));
	}

	void WriteU64LE(TArray<uint8>& Out, uint64 Value)
	{
		for (int32 Shift = 0; Shift < 64; Shift += 8)
		{
			Out.Add(static_cast<uint8>((Value >> Shift) & 0xFFull));
		}
	}

	void WriteI32LE(TArray<uint8>& Out, int32 Value)
	{
		WriteU32LE(Out, static_cast<uint32>(Value));
	}

	uint8 ReadU8(const uint8* Data, int32& Cursor)
	{
		return Data[Cursor++];
	}

	uint16 ReadU16LE(const uint8* Data, int32& Cursor)
	{
		const uint16 Value =
			  static_cast<uint16>(Data[Cursor])
			| static_cast<uint16>(static_cast<uint16>(Data[Cursor + 1]) << 8);
		Cursor += 2;
		return Value;
	}

	uint32 ReadU32LE(const uint8* Data, int32& Cursor)
	{
		const uint32 Value =
			  static_cast<uint32>(Data[Cursor])
			| (static_cast<uint32>(Data[Cursor + 1]) <<  8)
			| (static_cast<uint32>(Data[Cursor + 2]) << 16)
			| (static_cast<uint32>(Data[Cursor + 3]) << 24);
		Cursor += 4;
		return Value;
	}

	uint64 ReadU64LE(const uint8* Data, int32& Cursor)
	{
		uint64 Value = 0;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Value |= static_cast<uint64>(Data[Cursor + Index]) << (Index * 8);
		}
		Cursor += 8;
		return Value;
	}

	int32 ReadI32LE(const uint8* Data, int32& Cursor)
	{
		return static_cast<int32>(ReadU32LE(Data, Cursor));
	}
}

void SerializeTerrainOp(const FTerrainOp& Op, TArray<uint8>& OutBytes)
{
	const int32 StartNum = OutBytes.Num();
	OutBytes.Reserve(StartNum + TerrainOpEncodedSize);

	WriteU64LE(OutBytes, Op.OpSeq);                                 //  0 ..  7
	WriteU64LE(OutBytes, Op.TransactionId);                         //  8 .. 15
	WriteU8   (OutBytes, static_cast<uint8>(Op.Kind));              // 16
	WriteU8   (OutBytes, static_cast<uint8>(Op.Shape));             // 17
	WriteU8   (OutBytes, static_cast<uint8>(Op.Source));            // 18
	WriteU32LE(OutBytes, Op.SourceId);                              // 19 .. 22
	WriteU32LE(OutBytes, Op.ToolId);                                // 23 .. 26
	WriteI32LE(OutBytes, Op.CentreVox.X);                           // 27 .. 30
	WriteI32LE(OutBytes, Op.CentreVox.Y);                           // 31 .. 34
	WriteI32LE(OutBytes, Op.CentreVox.Z);                           // 35 .. 38
	WriteI32LE(OutBytes, Op.RadiusVoxQ16);                          // 39 .. 42
	WriteI32LE(OutBytes, Op.ExtentVox.X);                           // 43 .. 46
	WriteI32LE(OutBytes, Op.ExtentVox.Y);                           // 47 .. 50
	WriteI32LE(OutBytes, Op.ExtentVox.Z);                           // 51 .. 54
	WriteU16LE(OutBytes, Op.MaterialId);                            // 55 .. 56
	WriteU8   (OutBytes, Op.Flags);                                 // 57

	checkf(OutBytes.Num() - StartNum == TerrainOpEncodedSize,
		TEXT("FTerrainOp encoded to %d bytes; ARCHITECTURE.md §4.2 fixes the body at %d."),
		OutBytes.Num() - StartNum, TerrainOpEncodedSize);
}

bool DeserializeTerrainOp(TArrayView<const uint8> Bytes, FTerrainOp& OutOp)
{
	// Short buffer: refuse before touching Data. This is the only bounds check the reads
	// below need, because every read offset is a compile-time-known fraction of 58.
	if (Bytes.Num() < TerrainOpEncodedSize)
	{
		return false;
	}

	const uint8* Data = Bytes.GetData();
	int32 Cursor = 0;

	FTerrainOp Decoded;

	Decoded.OpSeq         = ReadU64LE(Data, Cursor);
	Decoded.TransactionId = ReadU64LE(Data, Cursor);

	const uint8 KindByte   = ReadU8(Data, Cursor);
	const uint8 ShapeByte  = ReadU8(Data, Cursor);
	const uint8 SourceByte = ReadU8(Data, Cursor);

	if (KindByte > TerrainOpKindMaxValue || ShapeByte > TerrainShapeMaxValue || SourceByte > TerrainSourceMaxValue)
	{
		return false;
	}

	Decoded.Kind   = static_cast<ETerrainOpKind>(KindByte);
	Decoded.Shape  = static_cast<ETerrainShape>(ShapeByte);
	Decoded.Source = static_cast<ETerrainSource>(SourceByte);

	Decoded.SourceId      = ReadU32LE(Data, Cursor);
	Decoded.ToolId        = ReadU32LE(Data, Cursor);
	Decoded.CentreVox.X   = ReadI32LE(Data, Cursor);
	Decoded.CentreVox.Y   = ReadI32LE(Data, Cursor);
	Decoded.CentreVox.Z   = ReadI32LE(Data, Cursor);
	Decoded.RadiusVoxQ16  = ReadI32LE(Data, Cursor);
	Decoded.ExtentVox.X   = ReadI32LE(Data, Cursor);
	Decoded.ExtentVox.Y   = ReadI32LE(Data, Cursor);
	Decoded.ExtentVox.Z   = ReadI32LE(Data, Cursor);
	Decoded.MaterialId    = ReadU16LE(Data, Cursor);
	Decoded.Flags         = ReadU8(Data, Cursor);

	check(Cursor == TerrainOpEncodedSize);

	// Committed only on full success, so a rejected buffer never half-writes the caller's op.
	OutOp = Decoded;
	return true;
}

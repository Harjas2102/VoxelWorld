// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "Misc/AutomationTest.h"
#include "TerrainOp.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * TerrainCore.Op.Codec.RoundTrip -- ARCHITECTURE.md 6.1.
 *
 * "Every FTerrainOp serialises and deserialises byte-identically, including negative
 *  coordinates and max radius. DEFINES THE 58-BYTE ENCODING."
 *
 * This test is the authority on the format (4.2). The 58 is asserted here, not merely
 * commented: if a field is added, widened or reordered, this test fails before any save
 * written by the old build becomes unreadable by the new one.
 *
 * Byte-identity alone is not sufficient evidence and is not what this test settles for. A
 * codec that swapped CentreVox.X with CentreVox.Y on both the write and the read path would
 * round-trip byte-identically and still be wrong, so every case checks decoded FIELDS as
 * well as re-encoded bytes.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainOpCodecRoundTripTest,
	"TerrainCore.Op.Codec.RoundTrip",
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

namespace
{
	bool TerrainOpFieldsEqual(const FTerrainOp& A, const FTerrainOp& B)
	{
		return A.OpSeq         == B.OpSeq
			&& A.TransactionId == B.TransactionId
			&& A.Kind          == B.Kind
			&& A.Shape         == B.Shape
			&& A.Source        == B.Source
			&& A.SourceId      == B.SourceId
			&& A.ToolId        == B.ToolId
			&& A.CentreVox     == B.CentreVox
			&& A.RadiusVoxQ16  == B.RadiusVoxQ16
			&& A.ExtentVox     == B.ExtentVox
			&& A.MaterialId    == B.MaterialId
			&& A.Flags         == B.Flags;
	}
}

bool FTerrainOpCodecRoundTripTest::RunTest(const FString& Parameters)
{
	// --- the size assertion, made directly ------------------------------------------------
	{
		FTerrainOp Op;
		TArray<uint8> Bytes;
		SerializeTerrainOp(Op, Bytes);

		// Logged as well as asserted. "Result={Success}" is the absence of a failure; this line
		// is the measurement itself, in the log, where a reader can see the number.
		AddInfo(FString::Printf(TEXT("Encoded FTerrainOp body measured %d bytes (ARCHITECTURE.md 4.2 fixes it at 58)"),
			Bytes.Num()));

		TestEqual(TEXT("Encoded FTerrainOp body is exactly 58 bytes (ARCHITECTURE.md 4.2)"),
			Bytes.Num(), 58);
		TestEqual(TEXT("TerrainOpEncodedSize agrees with the encoder"),
			TerrainOpEncodedSize, 58);

		// The encoder must append, not overwrite: the journal writes ops back to back.
		SerializeTerrainOp(Op, Bytes);
		TestEqual(TEXT("A second op appends another 58 bytes"), Bytes.Num(), 116);
	}

	// --- the round-trip cases -------------------------------------------------------------
	auto CheckRoundTrip = [this](const TCHAR* Label, const FTerrainOp& Original)
	{
		TArray<uint8> First;
		SerializeTerrainOp(Original, First);

		if (!TestEqual(FString::Printf(TEXT("%s: encodes to 58 bytes"), Label), First.Num(), 58))
		{
			return;
		}

		FTerrainOp Decoded;
		if (!TestTrue(FString::Printf(TEXT("%s: decodes"), Label),
			DeserializeTerrainOp(First, Decoded)))
		{
			return;
		}

		TestTrue(FString::Printf(TEXT("%s: decoded fields match the original"), Label),
			TerrainOpFieldsEqual(Original, Decoded));

		TArray<uint8> Second;
		SerializeTerrainOp(Decoded, Second);
		TestTrue(FString::Printf(TEXT("%s: re-encodes byte-identically"), Label),
			First == Second);
	};

	// Zeroed op -- every default from 4.2.
	CheckRoundTrip(TEXT("zeroed"), FTerrainOp());

	// All-max op -- every field at the top of its range at once.
	FTerrainOp MaxOp;
	MaxOp.OpSeq         = MAX_uint64;
	MaxOp.TransactionId = MAX_uint64;
	MaxOp.Kind          = ETerrainOpKind::Paint;
	MaxOp.Shape         = ETerrainShape::Box;
	MaxOp.Source        = ETerrainSource::Worldgen;
	MaxOp.SourceId      = MAX_uint32;
	MaxOp.ToolId        = MAX_uint32;
	MaxOp.CentreVox     = FIntVector(MAX_int32, MAX_int32, MAX_int32);
	MaxOp.RadiusVoxQ16  = MAX_int32;                 // max radius, 6.1
	MaxOp.ExtentVox     = FIntVector(MAX_int32, MAX_int32, MAX_int32);
	MaxOp.MaterialId    = MAX_uint16;
	MaxOp.Flags         = MAX_uint8;
	CheckRoundTrip(TEXT("all-max"), MaxOp);

	// Negative CentreVox and ExtentVox in all eight octants. A dig west of and below the
	// origin is the common case, not the exotic one: the T-101A PlayerStart is at x = -8228.
	for (int32 Octant = 0; Octant < 8; ++Octant)
	{
		const int32 SignX = (Octant & 1) ? -1 : 1;
		const int32 SignY = (Octant & 2) ? -1 : 1;
		const int32 SignZ = (Octant & 4) ? -1 : 1;

		FTerrainOp Op;
		Op.OpSeq         = 0x0123456789ABCDEFull;
		Op.TransactionId = 0xFEDCBA9876543210ull;
		Op.Kind          = ETerrainOpKind::Remove;
		Op.Shape         = ETerrainShape::Box;
		Op.Source        = ETerrainSource::Player;
		Op.SourceId      = 0xDEADBEEFu;
		Op.ToolId        = 0x0BADF00Du;
		Op.CentreVox     = FIntVector(SignX * 1234567, SignY * 89, SignZ * 2147483647);
		Op.RadiusVoxQ16  = SignX * 655360;
		Op.ExtentVox     = FIntVector(SignX * 7, SignY * 65536, SignZ * 2147483647);
		Op.MaterialId    = 0xBEEF;
		Op.Flags         = 0xA5;

		CheckRoundTrip(*FString::Printf(TEXT("octant %d"), Octant), Op);
	}

	// The extreme signed values on their own, both ends.
	{
		FTerrainOp Op;
		Op.CentreVox    = FIntVector(MIN_int32, MIN_int32, MIN_int32);
		Op.ExtentVox    = FIntVector(MIN_int32, 0, MAX_int32);
		Op.RadiusVoxQ16 = MIN_int32;
		CheckRoundTrip(TEXT("min-signed"), Op);

		Op.RadiusVoxQ16 = MAX_int32;
		CheckRoundTrip(TEXT("max radius"), Op);
	}

	// Every ETerrainOpKind x ETerrainShape x ETerrainSource combination.
	for (uint8 KindByte = 0; KindByte <= TerrainOpKindMaxValue; ++KindByte)
	{
		for (uint8 ShapeByte = 0; ShapeByte <= TerrainShapeMaxValue; ++ShapeByte)
		{
			for (uint8 SourceByte = 0; SourceByte <= TerrainSourceMaxValue; ++SourceByte)
			{
				FTerrainOp Op;
				Op.Kind   = static_cast<ETerrainOpKind>(KindByte);
				Op.Shape  = static_cast<ETerrainShape>(ShapeByte);
				Op.Source = static_cast<ETerrainSource>(SourceByte);
				Op.OpSeq  = 1 + KindByte * 100 + ShapeByte * 10 + SourceByte;

				CheckRoundTrip(*FString::Printf(TEXT("enum %u/%u/%u"), KindByte, ShapeByte, SourceByte), Op);
			}
		}
	}

	// --- truncation: 0..57 bytes all fail cleanly -----------------------------------------
	{
		TArray<uint8> Full;
		SerializeTerrainOp(MaxOp, Full);

		for (int32 Length = 0; Length < 58; ++Length)
		{
			// A fresh, exactly-sized allocation per length. A reader that walked past the end
			// would read past a real heap allocation here, not into the tail of a 58-byte
			// buffer that happens to still be there -- so a bounds bug shows up as a crash or
			// an ASan report, not as a pass.
			TArray<uint8> Truncated;
			Truncated.Append(Full.GetData(), Length);
			check(Truncated.Num() == Length);

			FTerrainOp Sentinel;
			Sentinel.OpSeq = 0x1122334455667788ull;
			const FTerrainOp Before = Sentinel;

			const bool bDecoded = DeserializeTerrainOp(
				TArrayView<const uint8>(Truncated.GetData(), Truncated.Num()), Sentinel);

			TestFalse(FString::Printf(TEXT("truncated to %d bytes fails"), Length), bDecoded);
			TestTrue(FString::Printf(TEXT("truncated to %d bytes leaves the out-op untouched"), Length),
				TerrainOpFieldsEqual(Before, Sentinel));
		}

		// The full buffer still decodes, so the failures above are the length check doing its
		// job and not the codec being broken outright.
		FTerrainOp Decoded;
		TestTrue(TEXT("the untruncated 58-byte buffer decodes"), DeserializeTerrainOp(Full, Decoded));

		// A longer buffer is accepted; only the first 58 bytes are consumed.
		TArray<uint8> Longer = Full;
		Longer.Add(0xFF);
		Longer.Add(0x00);
		FTerrainOp FromLonger;
		TestTrue(TEXT("a 60-byte buffer decodes its first op"), DeserializeTerrainOp(Longer, FromLonger));
		TestTrue(TEXT("the trailing bytes do not change the decoded op"),
			TerrainOpFieldsEqual(Decoded, FromLonger));
	}

	// --- out-of-range enum bytes fail ------------------------------------------------------
	{
		// Offsets fixed by the format: Kind 16, Shape 17, Source 18.
		const int32 EnumOffsets[3] = { 16, 17, 18 };
		const uint8 MaxValues[3] =
		{
			TerrainOpKindMaxValue,
			TerrainShapeMaxValue,
			TerrainSourceMaxValue,
		};
		const TCHAR* EnumNames[3] = { TEXT("Kind"), TEXT("Shape"), TEXT("Source") };

		FTerrainOp Base;
		Base.OpSeq = 42;

		for (int32 Index = 0; Index < 3; ++Index)
		{
			const uint8 BadValues[2] = { static_cast<uint8>(MaxValues[Index] + 1), 0xFF };

			for (const uint8 BadValue : BadValues)
			{
				TArray<uint8> Bytes;
				SerializeTerrainOp(Base, Bytes);
				Bytes[EnumOffsets[Index]] = BadValue;

				FTerrainOp Decoded;
				TestFalse(FString::Printf(TEXT("%s = %u is rejected"), EnumNames[Index], BadValue),
					DeserializeTerrainOp(Bytes, Decoded));
			}

			// ...and the highest DEFINED value at the same offset is still accepted, so the
			// check above is a range check and not an off-by-one that rejects a legal op.
			TArray<uint8> Bytes;
			SerializeTerrainOp(Base, Bytes);
			Bytes[EnumOffsets[Index]] = MaxValues[Index];

			FTerrainOp Decoded;
			TestTrue(FString::Printf(TEXT("%s = %u (the highest defined value) is accepted"),
				EnumNames[Index], MaxValues[Index]),
				DeserializeTerrainOp(Bytes, Decoded));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainPersistenceRecords.h"

#include "TerrainPersistenceIndex.h"

#include "Hash/Blake3.h"

/**
 * The schema-2 record bodies (Docs/proposals/P-004 sections 4-9).
 *
 * PERMANENT FORMAT. Every offset below is what a saved world means. Under AGENTS.md section 4
 * a change here is a save-format change with a migration path, not an edit.
 *
 * Every encoder validates its caps and field rules BEFORE writing a byte, and every decoder
 * assigns the caller's struct only after the whole body has decoded and been consumed exactly.
 * Both halves matter: an encoder that writes then fails leaves a partial object on a disk that
 * a later boot has to make sense of, and a decoder that fills in as it goes hands a caller a
 * half-valid record on failure.
 */

namespace
{
	/** ASCII "VXJR". Byte by byte, so the constant cannot acquire an endianness. */
	constexpr uint8 RecordMagic[4] = { 0x56, 0x58, 0x4A, 0x52 };
	constexpr uint8 RecordVersion  = 2;

	/** Offset of the body inside a frame, and the trailing checksum width. */
	constexpr int32 RecordBodyOffset   = 12;
	constexpr int32 RecordChecksumSize = 8;

	/** Finishes a frame: back-patches the length and appends the checksum over [0, Len-8). */
	void FinishRecordFrame(TArray<uint8>& Frame, int32 FrameStart)
	{
		const int32 TotalLength = (Frame.Num() - FrameStart) + RecordChecksumSize;

		// The length field sits at frame offset 4. It is the one value that cannot be written
		// in stream order, because it counts bytes that do not exist yet.
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Frame[FrameStart + 4 + Index] = static_cast<uint8>((static_cast<uint32>(TotalLength) >> (Index * 8)) & 0xFFu);
		}

		const uint64 Checksum = TerrainPersistChecksum(
			TArrayView<const uint8>(Frame.GetData() + FrameStart, Frame.Num() - FrameStart));

		FTerrainByteWriter Writer(Frame);
		Writer.WriteU64(Checksum);

		check(Frame.Num() - FrameStart == TotalLength);
	}

	void WriteFrameHeader(FTerrainByteWriter& Writer, ETerrainJournalRecordType Type)
	{
		Writer.WriteBytes(RecordMagic, 4);   //  0 ..  3
		Writer.WriteU32(0);                  //  4 ..  7  back-patched by FinishRecordFrame
		Writer.WriteU8(static_cast<uint8>(Type));   //  8
		Writer.WriteU8(RecordVersion);              //  9
		Writer.WriteZero(2);                        // 10 .. 11
	}

	bool IsAsciiSafeName(const ANSICHAR* Data, int32 Length)
	{
		for (int32 Index = 0; Index < Length; ++Index)
		{
			if (Data[Index] == '\0')
			{
				return false;   // no embedded NUL: the length is the only terminator
			}
		}
		return true;
	}
}

// ==== 4. base descriptor ================================================

ETerrainPersistError TerrainPersistEncodeBaseDescriptorBody(
	const FTerrainBaseDescriptor& In, TArray<uint8>& OutBody)
{
	const FTCHARToUTF8 GeneratorUtf8(*In.GeneratorName);
	const FTCHARToUTF8 BackendUtf8(*In.BackendName);

	if (GeneratorUtf8.Length() > TerrainPersistMaxNameBytes || BackendUtf8.Length() > TerrainPersistMaxNameBytes)
	{
		return ETerrainPersistError::CapExceeded;
	}
	if (In.AuthoredStamps.Num() > TerrainPersistMaxAuthoredStamps)
	{
		return ETerrainPersistError::CapExceeded;
	}
	if (!IsAsciiSafeName(GeneratorUtf8.Get(), GeneratorUtf8.Length())
		|| !IsAsciiSafeName(BackendUtf8.Get(), BackendUtf8.Length()))
	{
		return ETerrainPersistError::FieldOutOfRange;
	}
	if (In.VoxelSizeMicrometres <= 0 || In.ChunkSizeVox <= 0)
	{
		return ETerrainPersistError::FieldOutOfRange;
	}
	if (In.WorldBoundsVox.IsEmpty())
	{
		return ETerrainPersistError::FieldOutOfRange;
	}

	const int32 TotalLength = TerrainPersistBaseDescriptorPrefix
		+ GeneratorUtf8.Length() + BackendUtf8.Length()
		+ In.AuthoredStamps.Num() * TerrainPersistDigestSize;
	if (TotalLength > TerrainPersistMaxBaseDescriptorBody)
	{
		return ETerrainPersistError::CapExceeded;
	}

	OutBody.Reset();
	OutBody.Reserve(TotalLength);
	FTerrainByteWriter Writer(OutBody);

	Writer.WriteI64(In.Seed);                                   //   0 ..   7
	Writer.WriteU32(In.GeneratorVersion);                       //   8 ..  11
	Writer.WriteU32(In.BackendKernelVersion);                   //  12 ..  15
	Writer.WriteDigest(In.GeneratorParamsDigest);               //  16 ..  47
	Writer.WriteI64(In.OriginWorldMicrometres[0]);              //  48 ..  55
	Writer.WriteI64(In.OriginWorldMicrometres[1]);              //  56 ..  63
	Writer.WriteI64(In.OriginWorldMicrometres[2]);              //  64 ..  71
	Writer.WriteI64(In.VoxelSizeMicrometres);                   //  72 ..  79
	Writer.WriteI32(In.ChunkSizeVox);                           //  80 ..  83
	Writer.WriteI32(In.WorldBoundsVox.Min.X);                   //  84 ..  87
	Writer.WriteI32(In.WorldBoundsVox.Min.Y);                   //  88 ..  91
	Writer.WriteI32(In.WorldBoundsVox.Min.Z);                   //  92 ..  95
	Writer.WriteI32(In.WorldBoundsVox.Max.X);                   //  96 ..  99
	Writer.WriteI32(In.WorldBoundsVox.Max.Y);                   // 100 .. 103
	Writer.WriteI32(In.WorldBoundsVox.Max.Z);                   // 104 .. 107
	Writer.WriteU8(In.ValueConfig);                             // 108
	Writer.WriteU8(In.EncodingRulesVersion);                    // 109
	Writer.WriteU16(In.MaterialCatalogVersion);                 // 110 .. 111
	Writer.WriteU16(In.MaterialCatalogCount);                   // 112 .. 113
	Writer.WriteU16(static_cast<uint16>(GeneratorUtf8.Length()));  // 114 .. 115
	Writer.WriteU16(static_cast<uint16>(BackendUtf8.Length()));    // 116 .. 117
	Writer.WriteU16(static_cast<uint16>(In.AuthoredStamps.Num())); // 118 .. 119
	Writer.WriteZero(2);                                        // 120 .. 121 reserved

	check(Writer.BytesWritten() == TerrainPersistBaseDescriptorPrefix);

	Writer.WriteBytes(reinterpret_cast<const uint8*>(GeneratorUtf8.Get()), GeneratorUtf8.Length());
	Writer.WriteBytes(reinterpret_cast<const uint8*>(BackendUtf8.Get()), BackendUtf8.Length());
	for (const FTerrainDigest& Stamp : In.AuthoredStamps)
	{
		Writer.WriteDigest(Stamp);
	}

	check(Writer.BytesWritten() == TotalLength);
	return ETerrainPersistError::None;
}

ETerrainPersistError TerrainPersistDecodeBaseDescriptorBody(
	TArrayView<const uint8> Body, FTerrainBaseDescriptor& Out)
{
	if (Body.Num() < TerrainPersistBaseDescriptorPrefix)
	{
		return ETerrainPersistError::ShortBuffer;
	}

	FTerrainByteReader Reader(Body);
	FTerrainBaseDescriptor Decoded;

	Decoded.Seed                 = Reader.ReadI64();
	Decoded.GeneratorVersion     = Reader.ReadU32();
	Decoded.BackendKernelVersion = Reader.ReadU32();
	Reader.ReadDigest(Decoded.GeneratorParamsDigest);
	Decoded.OriginWorldMicrometres[0] = Reader.ReadI64();
	Decoded.OriginWorldMicrometres[1] = Reader.ReadI64();
	Decoded.OriginWorldMicrometres[2] = Reader.ReadI64();
	Decoded.VoxelSizeMicrometres = Reader.ReadI64();
	Decoded.ChunkSizeVox         = Reader.ReadI32();
	Decoded.WorldBoundsVox.Min.X = Reader.ReadI32();
	Decoded.WorldBoundsVox.Min.Y = Reader.ReadI32();
	Decoded.WorldBoundsVox.Min.Z = Reader.ReadI32();
	Decoded.WorldBoundsVox.Max.X = Reader.ReadI32();
	Decoded.WorldBoundsVox.Max.Y = Reader.ReadI32();
	Decoded.WorldBoundsVox.Max.Z = Reader.ReadI32();
	Decoded.ValueConfig            = Reader.ReadU8();
	Decoded.EncodingRulesVersion   = Reader.ReadU8();
	Decoded.MaterialCatalogVersion = Reader.ReadU16();
	Decoded.MaterialCatalogCount   = Reader.ReadU16();

	const uint16 GeneratorNameLength = Reader.ReadU16();
	const uint16 BackendNameLength   = Reader.ReadU16();
	const uint16 AuthoredStampCount  = Reader.ReadU16();
	if (!Reader.ReadZero(2))
	{
		return Reader.Error();
	}

	if (GeneratorNameLength > TerrainPersistMaxNameBytes
		|| BackendNameLength > TerrainPersistMaxNameBytes
		|| AuthoredStampCount > TerrainPersistMaxAuthoredStamps)
	{
		return ETerrainPersistError::CapExceeded;
	}

	const TArrayView<const uint8> GeneratorBytes = Reader.ReadView(GeneratorNameLength);
	const TArrayView<const uint8> BackendBytes   = Reader.ReadView(BackendNameLength);
	for (int32 Index = 0; Index < AuthoredStampCount; ++Index)
	{
		FTerrainDigest Stamp;
		Reader.ReadDigest(Stamp);
		Decoded.AuthoredStamps.Add(Stamp);
	}

	if (!Reader.IsValid())
	{
		return Reader.Error();
	}
	if (!Reader.AtEnd())
	{
		return ETerrainPersistError::TrailingBytes;
	}

	if (!IsAsciiSafeName(reinterpret_cast<const ANSICHAR*>(GeneratorBytes.GetData()), GeneratorBytes.Num())
		|| !IsAsciiSafeName(reinterpret_cast<const ANSICHAR*>(BackendBytes.GetData()), BackendBytes.Num()))
	{
		return ETerrainPersistError::FieldOutOfRange;
	}
	if (Decoded.VoxelSizeMicrometres <= 0 || Decoded.ChunkSizeVox <= 0 || Decoded.WorldBoundsVox.IsEmpty())
	{
		return ETerrainPersistError::FieldOutOfRange;
	}

	Decoded.GeneratorName = FString(FUTF8ToTCHAR(
		reinterpret_cast<const ANSICHAR*>(GeneratorBytes.GetData()), GeneratorBytes.Num()));
	Decoded.BackendName = FString(FUTF8ToTCHAR(
		reinterpret_cast<const ANSICHAR*>(BackendBytes.GetData()), BackendBytes.Num()));

	Out = MoveTemp(Decoded);
	return ETerrainPersistError::None;
}

ETerrainPersistError TerrainPersistEncodeBaseDescriptorObject(
	const FTerrainBaseDescriptor& In,
	const FTerrainWorldId& World,
	const FTerrainStoreEpoch& Epoch,
	TArray<uint8>& OutObject,
	FTerrainDigest& OutBaseDigest)
{
	TArray<uint8> Body;
	const ETerrainPersistError Error = TerrainPersistEncodeBaseDescriptorBody(In, Body);
	if (Error != ETerrainPersistError::None)
	{
		return Error;
	}

	FTerrainPersistIdentity Identity;
	Identity.World      = World;
	Identity.Epoch      = Epoch;
	Identity.BaseDigest = TerrainPersistDigest(Body);   // the self-referential digest

	OutObject.Reset();
	const ETerrainPersistError ObjectError =
		TerrainPersistEncodeObject(ETerrainPersistObjectType::BaseDescriptor, Identity, Body, OutObject);
	if (ObjectError != ETerrainPersistError::None)
	{
		return ObjectError;
	}

	OutBaseDigest = Identity.BaseDigest;
	return ETerrainPersistError::None;
}

ETerrainPersistError TerrainPersistDecodeBaseDescriptorObject(
	TArrayView<const uint8> Object,
	FTerrainPersistIdentity& OutIdentity,
	FTerrainBaseDescriptor& Out)
{
	FTerrainPersistObjectHeader Header;
	TArrayView<const uint8> Body;

	// Null expected identity: the base descriptor's BaseDigest is the digest of its own body,
	// so it is checked against the body below rather than against a caller's expectation.
	const ETerrainPersistError Error = TerrainPersistDecodeObject(
		Object, ETerrainPersistObjectType::BaseDescriptor, nullptr, Header, Body);
	if (Error != ETerrainPersistError::None)
	{
		return Error;
	}

	if (TerrainPersistDigest(Body) != Header.Identity.BaseDigest)
	{
		return ETerrainPersistError::BaseMismatch;
	}

	FTerrainBaseDescriptor Decoded;
	const ETerrainPersistError BodyError = TerrainPersistDecodeBaseDescriptorBody(Body, Decoded);
	if (BodyError != ETerrainPersistError::None)
	{
		return BodyError;
	}

	OutIdentity = Header.Identity;
	Out = MoveTemp(Decoded);
	return ETerrainPersistError::None;
}

// ==== 5. chunk payload =================================================

bool TerrainPersistBuildDense(
	TArrayView<const int16> Densities, TArrayView<const uint16> Materials, TArray<uint8>& OutDense)
{
	if (Densities.Num() != TerrainChunkSampleCount || Materials.Num() != TerrainChunkSampleCount)
	{
		return false;
	}

	OutDense.Reset();
	OutDense.Reserve(TerrainPersistDenseBytes);
	FTerrainByteWriter Writer(OutDense);

	for (int32 Index = 0; Index < TerrainChunkSampleCount; ++Index)
	{
		Writer.WriteI16(Densities[Index]);
	}
	for (int32 Index = 0; Index < TerrainChunkSampleCount; ++Index)
	{
		Writer.WriteU16(Materials[Index]);
	}

	check(OutDense.Num() == TerrainPersistDenseBytes);
	return true;
}

int16 TerrainPersistDenseDensityAt(TArrayView<const uint8> Dense, int32 LocalIndex)
{
	// Bounds are a returned zero, not a check(): checks compile out of a shipping build, and
	// this accessor is public API over a buffer that came off a disk.
	if (Dense.Num() != TerrainPersistDenseBytes || LocalIndex < 0 || LocalIndex >= TerrainChunkSampleCount)
	{
		return 0;
	}

	const int32 Offset = LocalIndex * 2;
	return static_cast<int16>(
		static_cast<uint16>(Dense[Offset]) | (static_cast<uint16>(Dense[Offset + 1]) << 8));
}

uint16 TerrainPersistDenseMaterialAt(TArrayView<const uint8> Dense, int32 LocalIndex)
{
	if (Dense.Num() != TerrainPersistDenseBytes || LocalIndex < 0 || LocalIndex >= TerrainChunkSampleCount)
	{
		return 0;
	}

	const int32 Offset = TerrainChunkSampleCount * 2 + LocalIndex * 2;
	return static_cast<uint16>(
		static_cast<uint16>(Dense[Offset]) | (static_cast<uint16>(Dense[Offset + 1]) << 8));
}

ETerrainPersistError TerrainPersistEncodeChunkPayloadBody(
	const FTerrainChunkPayloadRecord& In, TArray<uint8>& OutBody)
{
	if (In.Encoding == ETerrainRegionEncoding::Empty)
	{
		// P-004 section 5.2. Empty lives in the index leaf and nowhere else, so that one
		// logical state never has two on-disk spellings for a validator to arbitrate.
		return ETerrainPersistError::EncodingNotPermitted;
	}

	int32 EncodingBytes = 0;
	if (In.Encoding == ETerrainRegionEncoding::Dense)
	{
		if (In.Dense.Num() != TerrainPersistDenseBytes || In.Sparse.Num() != 0)
		{
			return ETerrainPersistError::FieldOutOfRange;
		}
		EncodingBytes = TerrainPersistDenseBytes;
	}
	else
	{
		if (In.Dense.Num() != 0)
		{
			return ETerrainPersistError::FieldOutOfRange;
		}
		if (In.Sparse.Num() < 1 || In.Sparse.Num() > TerrainPersistMaxSparseSamples)
		{
			// Zero samples is not an empty SparseDiff -- it is Empty, which is the index's job.
			return ETerrainPersistError::FieldOutOfRange;
		}

		for (int32 Index = 0; Index < In.Sparse.Num(); ++Index)
		{
			if (In.Sparse[Index].LocalIndex >= TerrainChunkSampleCount)
			{
				return ETerrainPersistError::FieldOutOfRange;
			}
			if (Index > 0 && In.Sparse[Index].LocalIndex <= In.Sparse[Index - 1].LocalIndex)
			{
				return ETerrainPersistError::OrderViolation;
			}
		}
		EncodingBytes = 4 + In.Sparse.Num() * TerrainPersistSparseEntryBytes;
	}

	const int32 TotalLength = TerrainPersistChunkPayloadPrefix + EncodingBytes;
	if (TotalLength > TerrainPersistMaxChunkPayloadBody)
	{
		return ETerrainPersistError::CapExceeded;
	}

	OutBody.Reset();
	OutBody.Reserve(TotalLength);
	FTerrainByteWriter Writer(OutBody);

	Writer.WriteI32(In.Key.X);                              //  0 ..  3
	Writer.WriteI32(In.Key.Y);                              //  4 ..  7
	Writer.WriteI32(In.Key.Z);                              //  8 .. 11
	Writer.WriteU32(In.Rev);                                // 12 .. 15
	Writer.WriteU64(In.LastOpSeq);                          // 16 .. 23
	Writer.WriteU8(static_cast<uint8>(In.Encoding));        // 24
	Writer.WriteU32(In.GeneratorVersion);                   // 25 .. 28
	Writer.WriteU8(In.ValueConfig);                         // 29
	Writer.WriteZero(2);                                    // 30 .. 31 reserved

	check(Writer.BytesWritten() == TerrainPersistChunkPayloadPrefix);

	if (In.Encoding == ETerrainRegionEncoding::Dense)
	{
		Writer.WriteBytes(In.Dense);
	}
	else
	{
		Writer.WriteU32(static_cast<uint32>(In.Sparse.Num()));
		for (const FTerrainChunkSample& Sample : In.Sparse)
		{
			Writer.WriteU16(Sample.LocalIndex);
			Writer.WriteI16(Sample.Density);
			Writer.WriteU16(Sample.MaterialId);
		}
	}

	check(Writer.BytesWritten() == TotalLength);
	return ETerrainPersistError::None;
}

ETerrainPersistError TerrainPersistDecodeChunkPayloadBody(
	TArrayView<const uint8> Body,
	uint32 ExpectedGeneratorVersion,
	uint8 ExpectedValueConfig,
	FTerrainChunkPayloadRecord& Out)
{
	if (Body.Num() < TerrainPersistChunkPayloadPrefix)
	{
		return ETerrainPersistError::ShortBuffer;
	}

	FTerrainByteReader Reader(Body);
	FTerrainChunkPayloadRecord Decoded;

	Decoded.Key.X     = Reader.ReadI32();
	Decoded.Key.Y     = Reader.ReadI32();
	Decoded.Key.Z     = Reader.ReadI32();
	Decoded.Rev       = Reader.ReadU32();
	Decoded.LastOpSeq = Reader.ReadU64();

	const uint8 EncodingByte = Reader.ReadU8();
	Decoded.GeneratorVersion = Reader.ReadU32();
	Decoded.ValueConfig      = Reader.ReadU8();
	if (!Reader.ReadZero(2))
	{
		return Reader.Error();
	}

	if (EncodingByte == static_cast<uint8>(ETerrainRegionEncoding::Empty))
	{
		return ETerrainPersistError::EncodingNotPermitted;
	}
	if (EncodingByte > static_cast<uint8>(ETerrainRegionEncoding::Empty))
	{
		return ETerrainPersistError::FieldOutOfRange;
	}
	Decoded.Encoding = static_cast<ETerrainRegionEncoding>(EncodingByte);

	if (Decoded.GeneratorVersion != ExpectedGeneratorVersion || Decoded.ValueConfig != ExpectedValueConfig)
	{
		// A chunk that disagrees with the base it is stored beside. P-003 section 6 requires
		// the exact base, and this is that failure rather than a tolerable difference.
		return ETerrainPersistError::BaseMismatch;
	}

	if (Decoded.Encoding == ETerrainRegionEncoding::Dense)
	{
		const TArrayView<const uint8> DenseView = Reader.ReadView(TerrainPersistDenseBytes);
		if (!Reader.IsValid())
		{
			return Reader.Error();
		}
		Decoded.Dense.Append(DenseView.GetData(), DenseView.Num());
	}
	else
	{
		const uint32 SampleCount = Reader.ReadU32();
		if (!Reader.IsValid())
		{
			return Reader.Error();
		}
		if (SampleCount == 0)
		{
			return ETerrainPersistError::FieldOutOfRange;
		}
		if (SampleCount > static_cast<uint32>(TerrainPersistMaxSparseSamples))
		{
			return ETerrainPersistError::CapExceeded;
		}

		Decoded.Sparse.Reserve(static_cast<int32>(SampleCount));
		int32 PreviousIndex = -1;
		for (uint32 Index = 0; Index < SampleCount; ++Index)
		{
			FTerrainChunkSample Sample;
			Sample.LocalIndex = Reader.ReadU16();
			Sample.Density    = Reader.ReadI16();
			Sample.MaterialId = Reader.ReadU16();
			if (!Reader.IsValid())
			{
				return Reader.Error();
			}
			if (Sample.LocalIndex >= TerrainChunkSampleCount)
			{
				return ETerrainPersistError::FieldOutOfRange;
			}
			if (static_cast<int32>(Sample.LocalIndex) <= PreviousIndex)
			{
				return ETerrainPersistError::OrderViolation;
			}
			PreviousIndex = static_cast<int32>(Sample.LocalIndex);
			Decoded.Sparse.Add(Sample);
		}
	}

	if (!Reader.IsValid())
	{
		return Reader.Error();
	}
	if (!Reader.AtEnd())
	{
		return ETerrainPersistError::TrailingBytes;
	}

	Out = MoveTemp(Decoded);
	return ETerrainPersistError::None;
}

ETerrainPersistError TerrainPersistChooseChunkEncoding(
	const FTerrainChunkKey& Key,
	FTerrainRev Rev,
	FTerrainOpSeq LastOpSeq,
	uint32 GeneratorVersion,
	uint8 ValueConfig,
	TArrayView<const uint8> CurrentDense,
	TArrayView<const uint8> BaseDense,
	FTerrainChunkPayloadRecord& OutRecord)
{
	if (CurrentDense.Num() != TerrainPersistDenseBytes || BaseDense.Num() != TerrainPersistDenseBytes)
	{
		return ETerrainPersistError::FieldOutOfRange;
	}

	FTerrainChunkPayloadRecord Record;
	Record.Key              = Key;
	Record.Rev              = Rev;
	Record.LastOpSeq        = LastOpSeq;
	Record.GeneratorVersion = GeneratorVersion;
	Record.ValueConfig      = ValueConfig;

	// Density AND material, because provenance is not equality (P-003 section 6): a chunk the
	// backend happens to have loaded is not thereby different from the base, and a chunk whose
	// densities match but whose materials do not is not pristine.
	for (int32 Index = 0; Index < TerrainChunkSampleCount; ++Index)
	{
		const int16  CurrentDensity  = TerrainPersistDenseDensityAt(CurrentDense, Index);
		const uint16 CurrentMaterial = TerrainPersistDenseMaterialAt(CurrentDense, Index);
		if (CurrentDensity != TerrainPersistDenseDensityAt(BaseDense, Index)
			|| CurrentMaterial != TerrainPersistDenseMaterialAt(BaseDense, Index))
		{
			FTerrainChunkSample Sample;
			Sample.LocalIndex = static_cast<uint16>(Index);
			Sample.Density    = CurrentDensity;
			Sample.MaterialId = CurrentMaterial;
			Record.Sparse.Add(Sample);
		}
	}

	if (Record.Sparse.Num() == 0)
	{
		Record.Encoding = ETerrainRegionEncoding::Empty;
		Record.Sparse.Reset();
		OutRecord = MoveTemp(Record);
		return ETerrainPersistError::None;
	}

	const int32 SparseBody = TerrainPersistChunkPayloadPrefix + 4 + Record.Sparse.Num() * TerrainPersistSparseEntryBytes;
	const int32 DenseBody  = TerrainPersistChunkPayloadPrefix + TerrainPersistDenseBytes;

	if (SparseBody < DenseBody)
	{
		Record.Encoding = ETerrainRegionEncoding::SparseDiff;
	}
	else
	{
		// Dense on ties, and Dense whenever it is not larger.
		Record.Encoding = ETerrainRegionEncoding::Dense;
		Record.Sparse.Reset();
		Record.Dense.Append(CurrentDense.GetData(), CurrentDense.Num());
	}

	OutRecord = MoveTemp(Record);
	return ETerrainPersistError::None;
}

// ==== 7. checkpoint descriptor =========================================

ETerrainPersistError TerrainPersistEncodeCheckpointBody(
	const FTerrainCheckpointDescriptor& In, TArray<uint8>& OutBody)
{
	// The two states have to agree, or a boot would have to guess which field was the truth.
	if (In.bHasRootPage)
	{
		if (In.RootPageLength == 0 || In.RootPageDigest.IsZero()
			|| In.RootPageLength > static_cast<uint32>(TerrainPersistObjectHeaderSize + TerrainPersistMaxIndexPageBody))
		{
			return ETerrainPersistError::FieldOutOfRange;
		}
	}
	else if (In.RootPageLength != 0 || !In.RootPageDigest.IsZero())
	{
		return ETerrainPersistError::FieldOutOfRange;
	}

	OutBody.Reset();
	OutBody.Reserve(TerrainPersistCheckpointBodySize);
	FTerrainByteWriter Writer(OutBody);

	Writer.WriteU64(In.G);                                      //  0 ..  7
	Writer.WriteU64(In.Generation);                             //  8 .. 15
	Writer.WriteI64(In.CreatedUtcMillis);                       // 16 .. 23
	Writer.WriteU32(In.bHasRootPage ? 1u : 0u);                 // 24 .. 27
	Writer.WriteU32(In.RootPageLength);                         // 28 .. 31
	Writer.WriteDigest(In.RootPageDigest);                      // 32 .. 63
	Writer.WriteU64(In.LeafKeyCount);                           // 64 .. 71
	Writer.WriteU64(In.TotalPayloadBytes);                      // 72 .. 79

	check(Writer.BytesWritten() == TerrainPersistCheckpointBodySize);
	return ETerrainPersistError::None;
}

ETerrainPersistError TerrainPersistDecodeCheckpointBody(
	TArrayView<const uint8> Body, FTerrainCheckpointDescriptor& Out)
{
	if (Body.Num() != TerrainPersistCheckpointBodySize)
	{
		return Body.Num() < TerrainPersistCheckpointBodySize
			? ETerrainPersistError::ShortBuffer : ETerrainPersistError::TrailingBytes;
	}

	FTerrainByteReader Reader(Body);
	FTerrainCheckpointDescriptor Decoded;

	Decoded.G                = Reader.ReadU64();
	Decoded.Generation       = Reader.ReadU64();
	Decoded.CreatedUtcMillis = Reader.ReadI64();

	const uint32 Flags = Reader.ReadU32();
	if ((Flags & ~1u) != 0)
	{
		return ETerrainPersistError::ReservedNotZero;
	}
	Decoded.bHasRootPage = (Flags & 1u) != 0;

	Decoded.RootPageLength = Reader.ReadU32();
	Reader.ReadDigest(Decoded.RootPageDigest);
	Decoded.LeafKeyCount      = Reader.ReadU64();
	Decoded.TotalPayloadBytes = Reader.ReadU64();

	if (!Reader.IsValid()) { return Reader.Error(); }

	if (Decoded.bHasRootPage)
	{
		if (Decoded.RootPageLength == 0 || Decoded.RootPageDigest.IsZero()
			|| Decoded.RootPageLength > static_cast<uint32>(TerrainPersistObjectHeaderSize + TerrainPersistMaxIndexPageBody))
		{
			return ETerrainPersistError::FieldOutOfRange;
		}
	}
	else if (Decoded.RootPageLength != 0 || !Decoded.RootPageDigest.IsZero())
	{
		return ETerrainPersistError::FieldOutOfRange;
	}

	Out = Decoded;
	return ETerrainPersistError::None;
}

// ==== 8. root slot =====================================================

ETerrainPersistError TerrainPersistEncodeRootSlotBody(
	const FTerrainRootSlot& In, TArray<uint8>& OutBody)
{
	if (In.StoreFormatVersion != TerrainPersistSchemaVersion)
	{
		return ETerrainPersistError::UnsupportedSchema;
	}
	if (In.DescriptorLength == 0 || In.DescriptorDigest.IsZero()
		|| In.DescriptorLength > static_cast<uint32>(TerrainPersistObjectHeaderSize + TerrainPersistMaxCheckpointDescriptorBody))
	{
		return ETerrainPersistError::FieldOutOfRange;
	}

	OutBody.Reset();
	OutBody.Reserve(TerrainPersistSlotBodySize);
	FTerrainByteWriter Writer(OutBody);

	Writer.WriteU64(In.Generation);                             //  0 ..  7
	Writer.WriteU64(In.G);                                      //  8 .. 15
	Writer.WriteDigest(In.DescriptorDigest);                    // 16 .. 47
	Writer.WriteU32(In.DescriptorLength);                       // 48 .. 51
	Writer.WriteU16(In.StoreFormatVersion);                     // 52 .. 53
	Writer.WriteZero(2);                                        // 54 .. 55 reserved
	Writer.WriteI64(In.PublishedUtcMillis);                     // 56 .. 63

	check(Writer.BytesWritten() == TerrainPersistRootSlotFixedBytes);

	// The reserved tail is part of the body, so the one body checksum covers the whole slot.
	Writer.WriteZero(TerrainPersistSlotBodySize - TerrainPersistRootSlotFixedBytes);

	check(Writer.BytesWritten() == TerrainPersistSlotBodySize);
	return ETerrainPersistError::None;
}

ETerrainPersistError TerrainPersistDecodeRootSlotBody(
	TArrayView<const uint8> Body, FTerrainRootSlot& Out)
{
	if (Body.Num() != TerrainPersistSlotBodySize)
	{
		return Body.Num() < TerrainPersistSlotBodySize
			? ETerrainPersistError::ShortBuffer : ETerrainPersistError::TrailingBytes;
	}

	FTerrainByteReader Reader(Body);
	FTerrainRootSlot Decoded;

	Decoded.Generation = Reader.ReadU64();
	Decoded.G          = Reader.ReadU64();
	Reader.ReadDigest(Decoded.DescriptorDigest);
	Decoded.DescriptorLength   = Reader.ReadU32();
	Decoded.StoreFormatVersion = Reader.ReadU16();
	if (!Reader.ReadZero(2)) { return Reader.Error(); }
	Decoded.PublishedUtcMillis = Reader.ReadI64();

	if (!Reader.ReadZero(TerrainPersistSlotBodySize - TerrainPersistRootSlotFixedBytes))
	{
		return Reader.Error();
	}
	if (!Reader.IsValid()) { return Reader.Error(); }

	if (Decoded.StoreFormatVersion != TerrainPersistSchemaVersion)
	{
		return ETerrainPersistError::UnsupportedSchema;
	}
	if (Decoded.DescriptorLength == 0 || Decoded.DescriptorDigest.IsZero()
		|| Decoded.DescriptorLength > static_cast<uint32>(TerrainPersistObjectHeaderSize + TerrainPersistMaxCheckpointDescriptorBody))
	{
		return ETerrainPersistError::FieldOutOfRange;
	}

	Out = Decoded;
	return ETerrainPersistError::None;
}

// ==== 9.1 segment header ===============================================

ETerrainPersistError TerrainPersistEncodeSegmentHeaderBody(
	const FTerrainJournalSegmentHeader& In, TArray<uint8>& OutBody)
{
	if (In.bHasPredecessor)
	{
		if (In.PredecessorSegmentId == 0 || In.PredecessorSealDigest.IsZero())
		{
			return ETerrainPersistError::FieldOutOfRange;
		}
	}
	else if (In.PredecessorSegmentId != 0 || !In.PredecessorSealDigest.IsZero())
	{
		return ETerrainPersistError::FieldOutOfRange;
	}
	if (In.FirstOpSeq == 0 || In.SegmentId == 0)
	{
		// OpSeq is 1-based, so a segment claiming to start at 0 has no first record. Segment 0
		// is reserved likewise: the anchor uses PredecessorSegmentId == 0 to mean "none", and
		// one value cannot also name a real segment.
		return ETerrainPersistError::FieldOutOfRange;
	}

	OutBody.Reset();
	OutBody.Reserve(TerrainPersistSegmentHeaderBodySize);
	FTerrainByteWriter Writer(OutBody);

	Writer.WriteU64(In.SegmentId);                              //  0 ..  7
	Writer.WriteU64(In.FirstOpSeq);                             //  8 .. 15
	Writer.WriteU64(In.PredecessorSegmentId);                   // 16 .. 23
	Writer.WriteDigest(In.PredecessorSealDigest);               // 24 .. 55
	Writer.WriteI64(In.CreatedUtcMillis);                       // 56 .. 63
	Writer.WriteU32(In.bHasPredecessor ? 1u : 0u);              // 64 .. 67
	Writer.WriteZero(4);                                        // 68 .. 71 reserved

	check(Writer.BytesWritten() == TerrainPersistSegmentHeaderBodySize);
	return ETerrainPersistError::None;
}

ETerrainPersistError TerrainPersistDecodeSegmentHeaderBody(
	TArrayView<const uint8> Body, FTerrainJournalSegmentHeader& Out)
{
	if (Body.Num() != TerrainPersistSegmentHeaderBodySize)
	{
		return Body.Num() < TerrainPersistSegmentHeaderBodySize
			? ETerrainPersistError::ShortBuffer : ETerrainPersistError::TrailingBytes;
	}

	FTerrainByteReader Reader(Body);
	FTerrainJournalSegmentHeader Decoded;

	Decoded.SegmentId            = Reader.ReadU64();
	Decoded.FirstOpSeq           = Reader.ReadU64();
	Decoded.PredecessorSegmentId = Reader.ReadU64();
	Reader.ReadDigest(Decoded.PredecessorSealDigest);
	Decoded.CreatedUtcMillis = Reader.ReadI64();

	const uint32 Flags = Reader.ReadU32();
	if ((Flags & ~1u) != 0) { return ETerrainPersistError::ReservedNotZero; }
	Decoded.bHasPredecessor = (Flags & 1u) != 0;

	if (!Reader.ReadZero(4)) { return Reader.Error(); }
	if (!Reader.IsValid())   { return Reader.Error(); }

	if (Decoded.FirstOpSeq == 0 || Decoded.SegmentId == 0) { return ETerrainPersistError::FieldOutOfRange; }
	if (Decoded.bHasPredecessor)
	{
		if (Decoded.PredecessorSegmentId == 0 || Decoded.PredecessorSealDigest.IsZero())
		{
			return ETerrainPersistError::FieldOutOfRange;
		}
	}
	else if (Decoded.PredecessorSegmentId != 0 || !Decoded.PredecessorSealDigest.IsZero())
	{
		return ETerrainPersistError::FieldOutOfRange;
	}

	Out = Decoded;
	return ETerrainPersistError::None;
}

// ==== 9.5 anchor slot ==================================================

ETerrainPersistError TerrainPersistEncodeAnchorBody(
	const FTerrainJournalAnchor& In, TArray<uint8>& OutBody)
{
	if (In.ActiveSegmentId == 0 || In.ActiveSegmentFirstOpSeq == 0)
	{
		return ETerrainPersistError::FieldOutOfRange;
	}
	if (In.bHasPredecessor)
	{
		if (In.PredecessorSegmentId == 0 || In.PredecessorSealDigest.IsZero())
		{
			return ETerrainPersistError::FieldOutOfRange;
		}
	}
	else if (In.PredecessorSegmentId != 0 || !In.PredecessorSealDigest.IsZero() || In.PredecessorLastOpSeq != 0)
	{
		return ETerrainPersistError::FieldOutOfRange;
	}

	OutBody.Reset();
	OutBody.Reserve(TerrainPersistSlotBodySize);
	FTerrainByteWriter Writer(OutBody);

	Writer.WriteU64(In.AnchorGeneration);                       //  0 ..  7
	Writer.WriteU64(In.ActiveSegmentId);                        //  8 .. 15
	Writer.WriteU64(In.ActiveSegmentFirstOpSeq);                // 16 .. 23
	Writer.WriteDigest(In.PredecessorSealDigest);               // 24 .. 55
	Writer.WriteU64(In.PredecessorSegmentId);                   // 56 .. 63
	Writer.WriteU64(In.PredecessorLastOpSeq);                   // 64 .. 71
	Writer.WriteI64(In.PublishedUtcMillis);                     // 72 .. 79
	Writer.WriteU32(In.bHasPredecessor ? 1u : 0u);              // 80 .. 83
	Writer.WriteZero(4);                                        // 84 .. 87 reserved

	check(Writer.BytesWritten() == TerrainPersistAnchorFixedBytes);

	Writer.WriteZero(TerrainPersistSlotBodySize - TerrainPersistAnchorFixedBytes);

	check(Writer.BytesWritten() == TerrainPersistSlotBodySize);
	return ETerrainPersistError::None;
}

ETerrainPersistError TerrainPersistDecodeAnchorBody(
	TArrayView<const uint8> Body, FTerrainJournalAnchor& Out)
{
	if (Body.Num() != TerrainPersistSlotBodySize)
	{
		return Body.Num() < TerrainPersistSlotBodySize
			? ETerrainPersistError::ShortBuffer : ETerrainPersistError::TrailingBytes;
	}

	FTerrainByteReader Reader(Body);
	FTerrainJournalAnchor Decoded;

	Decoded.AnchorGeneration        = Reader.ReadU64();
	Decoded.ActiveSegmentId         = Reader.ReadU64();
	Decoded.ActiveSegmentFirstOpSeq = Reader.ReadU64();
	Reader.ReadDigest(Decoded.PredecessorSealDigest);
	Decoded.PredecessorSegmentId = Reader.ReadU64();
	Decoded.PredecessorLastOpSeq = Reader.ReadU64();
	Decoded.PublishedUtcMillis   = Reader.ReadI64();

	const uint32 Flags = Reader.ReadU32();
	if ((Flags & ~1u) != 0) { return ETerrainPersistError::ReservedNotZero; }
	Decoded.bHasPredecessor = (Flags & 1u) != 0;

	if (!Reader.ReadZero(4)) { return Reader.Error(); }
	if (!Reader.ReadZero(TerrainPersistSlotBodySize - TerrainPersistAnchorFixedBytes)) { return Reader.Error(); }
	if (!Reader.IsValid()) { return Reader.Error(); }

	if (Decoded.ActiveSegmentId == 0 || Decoded.ActiveSegmentFirstOpSeq == 0)
	{
		return ETerrainPersistError::FieldOutOfRange;
	}
	if (Decoded.bHasPredecessor)
	{
		if (Decoded.PredecessorSegmentId == 0 || Decoded.PredecessorSealDigest.IsZero())
		{
			return ETerrainPersistError::FieldOutOfRange;
		}
	}
	else if (Decoded.PredecessorSegmentId != 0 || !Decoded.PredecessorSealDigest.IsZero()
		|| Decoded.PredecessorLastOpSeq != 0)
	{
		return ETerrainPersistError::FieldOutOfRange;
	}

	Out = Decoded;
	return ETerrainPersistError::None;
}

// ==== 9.6 canonical intent digest ======================================

FTerrainDigest TerrainPersistComputeIntentDigest(
	const FTerrainOp& Op,
	const uint8 TokenDigest[TerrainPersistTokenDigestBytes],
	uint32 RequestId,
	uint16 ChildOrdinal,
	uint16 ChildCount)
{
	TArray<uint8> Canonical;
	Canonical.Reserve(TerrainOpEncodedSize + TerrainPersistTokenDigestBytes + 8);

	SerializeTerrainOp(Op, Canonical);
	check(Canonical.Num() == TerrainOpEncodedSize);

	// The intent exists before a sequence is assigned, so OpSeq -- bytes 0..7 of the op -- is
	// zeroed. Including the assigned sequence would make the digest useless for its one job.
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Canonical[Index] = 0;
	}

	FTerrainByteWriter Writer(Canonical);
	Writer.WriteBytes(TokenDigest, TerrainPersistTokenDigestBytes);
	Writer.WriteU32(RequestId);
	Writer.WriteU16(ChildOrdinal);
	Writer.WriteU16(ChildCount);

	check(Canonical.Num() == TerrainOpEncodedSize + TerrainPersistTokenDigestBytes + 8);
	return TerrainPersistDigest(Canonical);
}

// ==== 9.2 / 9.3 / 9.4 journal records ==================================

ETerrainPersistError TerrainPersistEncodeCommitRecord(
	const FTerrainJournalCommitRecord& In, TArray<uint8>& OutFrame)
{
	if (In.Physical.Num()      > TerrainPersistMaxPhysicalEntriesPerRecord
		|| In.ChangedKeys.Num()   > TerrainPersistMaxChangedKeysPerRecord
		|| In.EconomyDeltas.Num() > TerrainPersistMaxEconomyDeltasPerRecord)
	{
		return ETerrainPersistError::CapExceeded;
	}
	if (In.RequestId == 0 || In.ChildCount == 0 || In.ChildOrdinal >= In.ChildCount)
	{
		return ETerrainPersistError::FieldOutOfRange;
	}
	if (In.PhysicalAvailability == ETerrainPhysicalAvailability::Unavailable && In.Physical.Num() != 0)
	{
		// P-003 section 2: unknown must not be encodable as a measured zero, and the converse
		// -- a measured list flagged unavailable -- would be the same lie in the other direction.
		return ETerrainPersistError::FieldOutOfRange;
	}
	if (In.EconomyKind == ETerrainEconomyKind::NoEconomy
		&& (In.EconomyDeltas.Num() != 0 || In.EconomyPolicyVersion != 0))
	{
		return ETerrainPersistError::FieldOutOfRange;
	}

	// Changed keys: strictly ascending by the section 6.1 index key, and every revision must
	// actually advance. A record that claims a chunk changed without its revision moving is a
	// record that cannot be replayed against a revision check.
	for (int32 Index = 0; Index < In.ChangedKeys.Num(); ++Index)
	{
		if (In.ChangedKeys[Index].AfterRev <= In.ChangedKeys[Index].BeforeRev)
		{
			return ETerrainPersistError::FieldOutOfRange;
		}
		if (Index > 0)
		{
			const FTerrainIndexKey Previous = TerrainIndexKeyFromChunk(In.ChangedKeys[Index - 1].Key);
			const FTerrainIndexKey Current  = TerrainIndexKeyFromChunk(In.ChangedKeys[Index].Key);
			if (!(Previous < Current))
			{
				return ETerrainPersistError::OrderViolation;
			}
		}
	}

	const int32 TotalLength = TerrainPersistRecordFrameOverhead + TerrainPersistCommitRecordPrefix
		+ In.Physical.Num()      * TerrainPersistPhysicalEntryBytes
		+ In.ChangedKeys.Num()   * TerrainPersistChangedKeyEntryBytes
		+ In.EconomyDeltas.Num() * TerrainPersistEconomyDeltaBytes;
	if (TotalLength > TerrainPersistMaxJournalRecordBytes)
	{
		return ETerrainPersistError::CapExceeded;
	}

	const int32 FrameStart = OutFrame.Num();
	OutFrame.Reserve(FrameStart + TotalLength);
	FTerrainByteWriter Writer(OutFrame);

	WriteFrameHeader(Writer, ETerrainJournalRecordType::Commit);
	check(Writer.BytesWritten() == RecordBodyOffset);

	Writer.WriteU64(In.WorldTag);                                     //   0 ..   7
	Writer.WriteI64(In.ServerUtcMillis);                              //   8 ..  15
	SerializeTerrainOp(In.Op, OutFrame);                              //  16 ..  73
	Writer.WriteBytes(In.TokenDigest, TerrainPersistTokenDigestBytes); //  74 ..  89
	Writer.WriteU32(In.RequestId);                                    //  90 ..  93
	Writer.WriteU16(In.ChildOrdinal);                                 //  94 ..  95
	Writer.WriteU16(In.ChildCount);                                   //  96 ..  97
	Writer.WriteDigest(In.IntentDigest);                              //  98 .. 129
	Writer.WriteU8(static_cast<uint8>(In.EconomyKind));               // 130
	Writer.WriteU8(static_cast<uint8>(In.PhysicalAvailability));      // 131
	Writer.WriteU16(static_cast<uint16>(In.Physical.Num()));          // 132 .. 133
	Writer.WriteU16(static_cast<uint16>(In.ChangedKeys.Num()));       // 134 .. 135
	Writer.WriteU16(static_cast<uint16>(In.EconomyDeltas.Num()));     // 136 .. 137
	Writer.WriteU16(In.EconomyPolicyVersion);                         // 138 .. 139

	check(Writer.BytesWritten() == RecordBodyOffset + TerrainPersistCommitRecordPrefix);

	for (const FTerrainMaterialVolume& Entry : In.Physical)
	{
		Writer.WriteU16(Entry.MaterialId);
		Writer.WriteI64(Entry.MicroLitres);
	}
	for (const FTerrainChangedKeyEntry& Entry : In.ChangedKeys)
	{
		Writer.WriteI32(Entry.Key.X);
		Writer.WriteI32(Entry.Key.Y);
		Writer.WriteI32(Entry.Key.Z);
		Writer.WriteU32(Entry.BeforeRev);
		Writer.WriteU32(Entry.AfterRev);
	}
	for (const FTerrainEconomyDelta& Entry : In.EconomyDeltas)
	{
		Writer.WriteU64(Entry.OwnerId);
		Writer.WriteU64(Entry.ContainerId);
		Writer.WriteU32(Entry.ItemId);
		Writer.WriteI64(Entry.Count);
		Writer.WriteU32(Entry.Flags);
	}

	FinishRecordFrame(OutFrame, FrameStart);
	check(OutFrame.Num() - FrameStart == TotalLength);
	return ETerrainPersistError::None;
}

ETerrainPersistError TerrainPersistEncodeSealRecord(
	const FTerrainJournalSealRecord& In, TArray<uint8>& OutFrame)
{
	const int32 FrameStart = OutFrame.Num();
	OutFrame.Reserve(FrameStart + TerrainPersistRecordFrameOverhead + TerrainPersistSealRecordBodySize);
	FTerrainByteWriter Writer(OutFrame);

	WriteFrameHeader(Writer, ETerrainJournalRecordType::Seal);

	Writer.WriteU64(In.WorldTag);              //  0 ..  7
	Writer.WriteU64(In.LastOpSeq);             //  8 .. 15
	Writer.WriteU64(In.CommitRecordCount);     // 16 .. 23
	Writer.WriteDigest(In.RecordsDigest);      // 24 .. 55
	Writer.WriteI64(In.SealedUtcMillis);       // 56 .. 63

	check(Writer.BytesWritten() == RecordBodyOffset + TerrainPersistSealRecordBodySize);

	FinishRecordFrame(OutFrame, FrameStart);
	return ETerrainPersistError::None;
}

ETerrainPersistError TerrainPersistPeekRecordFrame(
	TArrayView<const uint8> Bytes,
	int32& OutLength,
	ETerrainJournalRecordType& OutType)
{
	OutLength = 0;

	if (Bytes.Num() < RecordBodyOffset)
	{
		return ETerrainPersistError::ShortBuffer;
	}

	FTerrainByteReader Reader(Bytes);

	uint8 Magic[4];
	Reader.ReadBytes(Magic, 4);
	if (FMemory::Memcmp(Magic, RecordMagic, 4) != 0)
	{
		return ETerrainPersistError::BadMagic;
	}

	const uint32 Length      = Reader.ReadU32();
	const uint8  TypeByte    = Reader.ReadU8();
	const uint8  VersionByte = Reader.ReadU8();
	if (!Reader.ReadZero(2))
	{
		return Reader.Error();
	}

	if (VersionByte != RecordVersion)
	{
		return ETerrainPersistError::UnsupportedSchema;
	}
	if (TypeByte > TerrainJournalRecordTypeMaxValue)
	{
		return ETerrainPersistError::FieldOutOfRange;
	}
	const ETerrainJournalRecordType Type = static_cast<ETerrainJournalRecordType>(TypeByte);

	const int32 MinimumLength = TerrainPersistRecordFrameOverhead
		+ (Type == ETerrainJournalRecordType::Commit
			? TerrainPersistCommitRecordPrefix : TerrainPersistSealRecordBodySize);
	if (Length < static_cast<uint32>(MinimumLength))
	{
		return ETerrainPersistError::FieldOutOfRange;
	}
	if (Length > static_cast<uint32>(TerrainPersistMaxJournalRecordBytes))
	{
		return ETerrainPersistError::CapExceeded;
	}
	if (static_cast<int64>(Length) > static_cast<int64>(Bytes.Num()))
	{
		// The caller decides whether this is a torn tail (legal only at the end of the active
		// segment) or corruption. This function does not get to make that call.
		return ETerrainPersistError::ShortBuffer;
	}

	FTerrainByteReader ChecksumField(
		TArrayView<const uint8>(Bytes.GetData() + Length - RecordChecksumSize, RecordChecksumSize));
	const uint64 Expected = ChecksumField.ReadU64();
	const uint64 Computed = TerrainPersistChecksum(
		TArrayView<const uint8>(Bytes.GetData(), static_cast<int32>(Length) - RecordChecksumSize));

	// OutLength is published even on a checksum failure: the frame header is structurally
	// valid, and a segment scanner needs the length to tell "the last region in the file is
	// damaged" (a legal torn tail) from "a record in the middle is damaged" (corruption).
	OutLength = static_cast<int32>(Length);
	OutType   = Type;

	if (Expected != Computed)
	{
		return ETerrainPersistError::BodyChecksumMismatch;
	}
	return ETerrainPersistError::None;
}

ETerrainPersistError TerrainPersistComputeRecordDigest(
	TArrayView<const uint8> Frame, FTerrainDigest& OutDigest)
{
	int32 Length = 0;
	ETerrainJournalRecordType Type = ETerrainJournalRecordType::Commit;
	const ETerrainPersistError Error = TerrainPersistPeekRecordFrame(Frame, Length, Type);
	if (Error != ETerrainPersistError::None)
	{
		return Error;
	}

	OutDigest = TerrainPersistDigest(TArrayView<const uint8>(
		Frame.GetData() + RecordBodyOffset, Length - RecordBodyOffset - RecordChecksumSize));
	return ETerrainPersistError::None;
}

ETerrainPersistError TerrainPersistDecodeCommitRecord(
	TArrayView<const uint8> Frame, uint64 ExpectedWorldTag, FTerrainJournalCommitRecord& Out)
{
	int32 Length = 0;
	ETerrainJournalRecordType Type = ETerrainJournalRecordType::Commit;
	const ETerrainPersistError FrameError = TerrainPersistPeekRecordFrame(Frame, Length, Type);
	if (FrameError != ETerrainPersistError::None)
	{
		return FrameError;
	}
	if (Type != ETerrainJournalRecordType::Commit)
	{
		return ETerrainPersistError::UnknownObjectType;
	}

	const TArrayView<const uint8> Body(
		Frame.GetData() + RecordBodyOffset, Length - RecordBodyOffset - RecordChecksumSize);

	FTerrainByteReader Reader(Body);
	FTerrainJournalCommitRecord Decoded;

	Decoded.WorldTag        = Reader.ReadU64();
	Decoded.ServerUtcMillis = Reader.ReadI64();

	const TArrayView<const uint8> OpBytes = Reader.ReadView(TerrainOpEncodedSize);
	if (!Reader.IsValid()) { return Reader.Error(); }
	if (!DeserializeTerrainOp(OpBytes, Decoded.Op))
	{
		// An op enum this build does not define. Never a silent cast to Remove.
		return ETerrainPersistError::FieldOutOfRange;
	}

	Reader.ReadBytes(Decoded.TokenDigest, TerrainPersistTokenDigestBytes);
	Decoded.RequestId    = Reader.ReadU32();
	Decoded.ChildOrdinal = Reader.ReadU16();
	Decoded.ChildCount   = Reader.ReadU16();
	Reader.ReadDigest(Decoded.IntentDigest);

	const uint8 EconomyByte  = Reader.ReadU8();
	const uint8 PhysicalByte = Reader.ReadU8();
	const uint16 PhysicalCount      = Reader.ReadU16();
	const uint16 ChangedKeyCount    = Reader.ReadU16();
	const uint16 EconomyDeltaCount  = Reader.ReadU16();
	Decoded.EconomyPolicyVersion    = Reader.ReadU16();

	if (!Reader.IsValid()) { return Reader.Error(); }

	if (EconomyByte > static_cast<uint8>(ETerrainEconomyKind::ExactDeltas)
		|| PhysicalByte > static_cast<uint8>(ETerrainPhysicalAvailability::Measured))
	{
		return ETerrainPersistError::FieldOutOfRange;
	}
	Decoded.EconomyKind          = static_cast<ETerrainEconomyKind>(EconomyByte);
	Decoded.PhysicalAvailability = static_cast<ETerrainPhysicalAvailability>(PhysicalByte);

	if (PhysicalCount > TerrainPersistMaxPhysicalEntriesPerRecord
		|| ChangedKeyCount > TerrainPersistMaxChangedKeysPerRecord
		|| EconomyDeltaCount > TerrainPersistMaxEconomyDeltasPerRecord)
	{
		return ETerrainPersistError::CapExceeded;
	}
	if (Decoded.RequestId == 0 || Decoded.ChildCount == 0 || Decoded.ChildOrdinal >= Decoded.ChildCount)
	{
		return ETerrainPersistError::FieldOutOfRange;
	}
	if (Decoded.PhysicalAvailability == ETerrainPhysicalAvailability::Unavailable && PhysicalCount != 0)
	{
		return ETerrainPersistError::FieldOutOfRange;
	}
	if (Decoded.EconomyKind == ETerrainEconomyKind::NoEconomy
		&& (EconomyDeltaCount != 0 || Decoded.EconomyPolicyVersion != 0))
	{
		return ETerrainPersistError::FieldOutOfRange;
	}
	if (ExpectedWorldTag != 0 && Decoded.WorldTag != ExpectedWorldTag)
	{
		// A record spliced out of another world's segment. Its own checksum is intact and it
		// is still not this world's history (P-004 section 9.2).
		return ETerrainPersistError::WorldMismatch;
	}

	Decoded.Physical.Reserve(PhysicalCount);
	for (uint16 Index = 0; Index < PhysicalCount; ++Index)
	{
		FTerrainMaterialVolume Entry;
		Entry.MaterialId  = Reader.ReadU16();
		Entry.MicroLitres = Reader.ReadI64();
		Decoded.Physical.Add(Entry);
	}

	Decoded.ChangedKeys.Reserve(ChangedKeyCount);
	for (uint16 Index = 0; Index < ChangedKeyCount; ++Index)
	{
		FTerrainChangedKeyEntry Entry;
		Entry.Key.X     = Reader.ReadI32();
		Entry.Key.Y     = Reader.ReadI32();
		Entry.Key.Z     = Reader.ReadI32();
		Entry.BeforeRev = Reader.ReadU32();
		Entry.AfterRev  = Reader.ReadU32();
		if (!Reader.IsValid()) { return Reader.Error(); }

		if (Entry.AfterRev <= Entry.BeforeRev)
		{
			return ETerrainPersistError::FieldOutOfRange;
		}
		if (Index > 0)
		{
			const FTerrainIndexKey Previous = TerrainIndexKeyFromChunk(Decoded.ChangedKeys[Index - 1].Key);
			const FTerrainIndexKey Current  = TerrainIndexKeyFromChunk(Entry.Key);
			if (!(Previous < Current))
			{
				return ETerrainPersistError::OrderViolation;
			}
		}
		Decoded.ChangedKeys.Add(Entry);
	}

	Decoded.EconomyDeltas.Reserve(EconomyDeltaCount);
	for (uint16 Index = 0; Index < EconomyDeltaCount; ++Index)
	{
		FTerrainEconomyDelta Entry;
		Entry.OwnerId     = Reader.ReadU64();
		Entry.ContainerId = Reader.ReadU64();
		Entry.ItemId      = Reader.ReadU32();
		Entry.Count       = Reader.ReadI64();
		Entry.Flags       = Reader.ReadU32();
		Decoded.EconomyDeltas.Add(Entry);
	}

	if (!Reader.IsValid()) { return Reader.Error(); }
	if (!Reader.AtEnd())   { return ETerrainPersistError::TrailingBytes; }

	Out = MoveTemp(Decoded);
	return ETerrainPersistError::None;
}

ETerrainPersistError TerrainPersistDecodeSealRecord(
	TArrayView<const uint8> Frame, uint64 ExpectedWorldTag, FTerrainJournalSealRecord& Out)
{
	int32 Length = 0;
	ETerrainJournalRecordType Type = ETerrainJournalRecordType::Commit;
	const ETerrainPersistError FrameError = TerrainPersistPeekRecordFrame(Frame, Length, Type);
	if (FrameError != ETerrainPersistError::None)
	{
		return FrameError;
	}
	if (Type != ETerrainJournalRecordType::Seal)
	{
		return ETerrainPersistError::UnknownObjectType;
	}

	const TArrayView<const uint8> Body(
		Frame.GetData() + RecordBodyOffset, Length - RecordBodyOffset - RecordChecksumSize);

	FTerrainByteReader Reader(Body);
	FTerrainJournalSealRecord Decoded;

	Decoded.WorldTag          = Reader.ReadU64();
	Decoded.LastOpSeq         = Reader.ReadU64();
	Decoded.CommitRecordCount = Reader.ReadU64();
	Reader.ReadDigest(Decoded.RecordsDigest);
	Decoded.SealedUtcMillis = Reader.ReadI64();

	if (!Reader.IsValid()) { return Reader.Error(); }
	if (!Reader.AtEnd())   { return ETerrainPersistError::TrailingBytes; }

	if (ExpectedWorldTag != 0 && Decoded.WorldTag != ExpectedWorldTag)
	{
		return ETerrainPersistError::WorldMismatch;
	}

	Out = Decoded;
	return ETerrainPersistError::None;
}

// ==== segment scanning =================================================

ETerrainPersistError TerrainPersistScanJournalSegment(
	TArrayView<const uint8> SegmentBytes,
	const FTerrainPersistIdentity& Identity,
	bool bActiveSegment,
	FTerrainJournalScanResult& Out)
{
	FTerrainPersistObjectHeader ObjectHeader;
	TArrayView<const uint8> HeaderBody;
	int32 Consumed = 0;

	const ETerrainPersistError HeaderError = TerrainPersistDecodeObjectPrefix(
		SegmentBytes, ETerrainPersistObjectType::JournalSegmentHeader, &Identity,
		ObjectHeader, HeaderBody, Consumed);
	if (HeaderError != ETerrainPersistError::None)
	{
		return HeaderError;
	}

	FTerrainJournalScanResult Result;
	const ETerrainPersistError BodyError = TerrainPersistDecodeSegmentHeaderBody(HeaderBody, Result.Header);
	if (BodyError != ETerrainPersistError::None)
	{
		return BodyError;
	}

	Result.FirstOpSeq = Result.Header.FirstOpSeq;
	Result.GoodBytes  = Consumed;

	const uint64 WorldTag = TerrainPersistWorldTag(Identity.World, Identity.Epoch);

	// RecordsDigest is accumulated over commit frames in order, exactly as the writer would
	// accumulate it, so a seal record's claim can be checked without a second pass.
	FBlake3 RecordsHash;

	int32 Cursor = Consumed;
	FTerrainOpSeq ExpectedOpSeq = Result.Header.FirstOpSeq;

	while (Cursor < SegmentBytes.Num())
	{
		if (Result.bSealed)
		{
			// A seal record is the last record in its segment. Anything after it is a protocol
			// violation, not a continuation.
			return ETerrainPersistError::OrderViolation;
		}

		const TArrayView<const uint8> Remaining(
			SegmentBytes.GetData() + Cursor, SegmentBytes.Num() - Cursor);

		int32 Length = 0;
		ETerrainJournalRecordType Type = ETerrainJournalRecordType::Commit;
		const ETerrainPersistError FrameError = TerrainPersistPeekRecordFrame(Remaining, Length, Type);

		if (FrameError != ETerrainPersistError::None)
		{
			// P-003 section 3 permits exactly one incomplete physical tail, and only at the end
			// of the ACTIVE segment. "At the end" is checked, not assumed: a damaged record in
			// the MIDDLE of a file is corruption and must fail closed, so a checksum failure
			// only counts as a tear when its frame actually reaches end of file.
			bool bTailShaped = false;
			if (FrameError == ETerrainPersistError::ShortBuffer)
			{
				// The frame does not fit in what remains, so it is necessarily the last region.
				bTailShaped = true;
			}
			else if (FrameError == ETerrainPersistError::BodyChecksumMismatch)
			{
				bTailShaped = Length > 0 && Cursor + Length == SegmentBytes.Num();
			}
			else if (FrameError == ETerrainPersistError::BadMagic)
			{
				// A torn append can leave the file extended with zeros rather than with bytes.
				// An all-zero remainder is that case; anything else is unexplained garbage.
				bTailShaped = true;
				for (int32 Index = Cursor; Index < SegmentBytes.Num(); ++Index)
				{
					if (SegmentBytes[Index] != 0)
					{
						bTailShaped = false;
						break;
					}
				}
			}

			if (bActiveSegment && bTailShaped)
			{
				Result.TornTailBytes = SegmentBytes.Num() - Cursor;
				Result.bTornTail     = true;
				break;
			}
			return FrameError;
		}

		const TArrayView<const uint8> Frame(SegmentBytes.GetData() + Cursor, Length);

		if (Type == ETerrainJournalRecordType::Commit)
		{
			FTerrainJournalCommitRecord Record;
			const ETerrainPersistError RecordError =
				TerrainPersistDecodeCommitRecord(Frame, WorldTag, Record);
			if (RecordError != ETerrainPersistError::None)
			{
				return RecordError;
			}
			if (Record.Op.OpSeq != ExpectedOpSeq)
			{
				// Contiguity is a property of the bytes, so it is checked here. A gap or a
				// duplicate is corruption, never something to interpolate over.
				return ETerrainPersistError::OrderViolation;
			}

			RecordsHash.Update(Frame.GetData(), static_cast<uint64>(Frame.Num()));

			Result.LastOpSeq = Record.Op.OpSeq;
			++Result.CommitRecordCount;
			++ExpectedOpSeq;
		}
		else
		{
			FTerrainJournalSealRecord Seal;
			const ETerrainPersistError SealError = TerrainPersistDecodeSealRecord(Frame, WorldTag, Seal);
			if (SealError != ETerrainPersistError::None)
			{
				return SealError;
			}
			if (Seal.CommitRecordCount != static_cast<uint64>(Result.CommitRecordCount)
				|| Seal.LastOpSeq != Result.LastOpSeq)
			{
				return ETerrainPersistError::FieldOutOfRange;
			}

			FTerrainDigest Computed;
			const FBlake3Hash Hash = RecordsHash.Finalize();
			FMemory::Memcpy(Computed.Bytes, Hash.GetBytes(), TerrainPersistDigestSize);
			if (Computed != Seal.RecordsDigest)
			{
				return ETerrainPersistError::BodyChecksumMismatch;
			}

			Result.bSealed = true;
			Result.Seal    = Seal;
		}

		Cursor += Length;
		Result.GoodBytes = Cursor;
	}

	if (!Result.bSealed)
	{
		const FBlake3Hash Hash = RecordsHash.Finalize();
		FMemory::Memcpy(Result.RecordsDigest.Bytes, Hash.GetBytes(), TerrainPersistDigestSize);
	}
	else
	{
		Result.RecordsDigest = Result.Seal.RecordsDigest;
	}

	Out = MoveTemp(Result);
	return ETerrainPersistError::None;
}

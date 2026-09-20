// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "TerrainPersistenceFormat.h"
#include "TerrainPersistenceRecords.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Shared schema-2 test fixtures (Docs/proposals/P-004 section 13).
 *
 * EVERY VALUE HERE IS FIXED. No clock, no random, no engine state, no configuration: the
 * golden vectors in TerrainPersistenceGoldenTest.cpp are the BLAKE3 of the bytes these
 * builders produce, so a fixture that varied between runs would make those constants
 * meaningless and the test a coin flip.
 *
 * They live in a header rather than in one .cpp because two test files need the same bytes,
 * and a duplicated fixture is a fixture that will eventually differ from itself. Everything
 * is `inline` so a unity build cannot produce a duplicate symbol.
 */
namespace TerrainPersistTest
{
	/** Deliberately non-zero identity bytes: a zero identity would hide a field that was never written. */
	inline FTerrainPersistIdentity MakeIdentity()
	{
		FTerrainPersistIdentity Identity;
		for (int32 Index = 0; Index < TerrainPersistIdBytes; ++Index)
		{
			Identity.World.Bytes[Index] = static_cast<uint8>(0x10 + Index);
			Identity.Epoch.Bytes[Index] = static_cast<uint8>(0xA0 + Index);
		}
		for (int32 Index = 0; Index < TerrainPersistDigestSize; ++Index)
		{
			Identity.BaseDigest.Bytes[Index] = static_cast<uint8>(0x40 + Index);
		}
		return Identity;
	}

	inline FTerrainDigest MakeDigest(uint8 Seed)
	{
		FTerrainDigest Digest;
		for (int32 Index = 0; Index < TerrainPersistDigestSize; ++Index)
		{
			Digest.Bytes[Index] = static_cast<uint8>(Seed + Index);
		}
		return Digest;
	}

	inline uint64 ReadU64At(const TArray<uint8>& Bytes, int32 Offset)
	{
		uint64 Value = 0;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Value |= static_cast<uint64>(Bytes[Offset + Index]) << (Index * 8);
		}
		return Value;
	}

	inline void WriteU64At(TArray<uint8>& Bytes, int32 Offset, uint64 Value)
	{
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Bytes[Offset + Index] = static_cast<uint8>((Value >> (Index * 8)) & 0xFFull);
		}
	}

	/**
	 * Recomputes both checksums after a deliberate mutation.
	 *
	 * Without this, every corrupt fixture would stop at HeaderChecksumMismatch and the checks
	 * further down the validation order -- the ones the fixture is actually about -- would
	 * never run, which is the failure mode the whole error taxonomy exists to prevent.
	 */
	inline void ResealObject(TArray<uint8>& Object)
	{
		const uint64 BodyLength = ReadU64At(Object, 72);
		const int32 BodyNum = static_cast<int32>(
			FMath::Min<uint64>(BodyLength, static_cast<uint64>(Object.Num() - TerrainPersistObjectHeaderSize)));
		const TArrayView<const uint8> Body(Object.GetData() + TerrainPersistObjectHeaderSize, BodyNum);

		WriteU64At(Object, 80, TerrainPersistChecksum(Body));
		WriteU64At(Object, 88, TerrainPersistChecksum(TArrayView<const uint8>(Object.GetData(), 88)));
	}

	/** A journal record frame's trailing checksum, after a deliberate mutation. */
	inline void ResealRecord(TArray<uint8>& Frame)
	{
		const int32 Length = Frame.Num();
		const uint64 Checksum = TerrainPersistChecksum(TArrayView<const uint8>(Frame.GetData(), Length - 8));
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Frame[Length - 8 + Index] = static_cast<uint8>((Checksum >> (Index * 8)) & 0xFFull);
		}
	}

	inline FTerrainBaseDescriptor MakeBaseDescriptor()
	{
		FTerrainBaseDescriptor Base;
		Base.Seed                      = -1234567890123LL;
		Base.GeneratorVersion          = 7;
		Base.BackendKernelVersion      = 3;
		Base.GeneratorParamsDigest     = MakeDigest(0x11);
		Base.OriginWorldMicrometres[0] = -5000000;
		Base.OriginWorldMicrometres[1] = 0;
		Base.OriginWorldMicrometres[2] = 12345678;
		Base.VoxelSizeMicrometres      = 500000;   // 50 cm, exactly
		Base.ChunkSizeVox              = TerrainChunkSizeVox;
		Base.WorldBoundsVox            = FTerrainBox(FIntVector(-512, -512, -512), FIntVector(512, 512, 512));
		Base.ValueConfig               = 1;
		Base.EncodingRulesVersion      = 1;
		Base.MaterialCatalogVersion    = 2;
		Base.MaterialCatalogCount      = 8;
		Base.GeneratorName             = TEXT("FTerrainWorldField");
		Base.BackendName               = TEXT("TerrainBackendVPLegacy");
		return Base;
	}

	inline FTerrainJournalCommitRecord MakeCommitRecord()
	{
		const FTerrainPersistIdentity Identity = MakeIdentity();

		FTerrainJournalCommitRecord Record;
		Record.WorldTag        = TerrainPersistWorldTag(Identity.World, Identity.Epoch);
		Record.ServerUtcMillis = 1789412345678LL;

		Record.Op.OpSeq         = 1;
		Record.Op.TransactionId = 99;
		Record.Op.Kind          = ETerrainOpKind::Remove;
		Record.Op.Shape         = ETerrainShape::Sphere;
		Record.Op.Source        = ETerrainSource::Player;
		Record.Op.SourceId      = 4;
		Record.Op.ToolId        = 0;
		Record.Op.CentreVox     = FIntVector(-13, 7, -41);
		Record.Op.RadiusVoxQ16  = 4 << 16;
		Record.Op.MaterialId    = 0;
		Record.Op.Flags         = 0;

		for (int32 Index = 0; Index < TerrainPersistTokenDigestBytes; ++Index)
		{
			Record.TokenDigest[Index] = static_cast<uint8>(0x70 + Index);
		}
		Record.RequestId    = 12;
		Record.ChildOrdinal = 0;
		Record.ChildCount   = 1;
		Record.IntentDigest = TerrainPersistComputeIntentDigest(
			Record.Op, Record.TokenDigest, Record.RequestId, Record.ChildOrdinal, Record.ChildCount);

		Record.EconomyKind          = ETerrainEconomyKind::NoEconomy;
		Record.PhysicalAvailability = ETerrainPhysicalAvailability::Measured;

		FTerrainMaterialVolume Volume;
		Volume.MaterialId  = 4;
		Volume.MicroLitres = 257000;
		Record.Physical.Add(Volume);

		// Built in ascending index-key order, because the encoder requires it. The Corrupt
		// test is where a descending list is proven to be rejected.
		const FTerrainChunkKey Keys[3] = {
			FTerrainChunkKey(-2, 0, -2),
			FTerrainChunkKey(-1, 0, -2),
			FTerrainChunkKey(-1, 0, -1),
		};
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FTerrainChangedKeyEntry Entry;
			Entry.Key       = Keys[Index];
			Entry.BeforeRev = 4;
			Entry.AfterRev  = 5;
			Record.ChangedKeys.Add(Entry);
		}
		return Record;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS

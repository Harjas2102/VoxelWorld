// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "Misc/AutomationTest.h"
#include "TerrainPersistenceFormat.h"
#include "TerrainPersistenceRecords.h"
#include "TerrainPersistenceIndex.h"
#include "TerrainPersistenceFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * TerrainCore.Persistence.Format.Golden -- the pinned schema-2 golden vectors (P-004 13).
 *
 * Every fixture is built from fixed inputs, so its bytes are a pure function of the format.
 * The constants below are the BLAKE3 of those bytes.
 *
 * WHY HEX RATHER THAN BINARY FIXTURE FILES. A .bin in Tests/Saves would be a golden fixture
 * nobody can review: the diff would say "binary files differ" and a reviewer would have to
 * take the change on trust. A 64-character hex string changes visibly in the diff, and the
 * test prints the measured value beside it, so a deliberate format change is an obvious edit
 * to this table and an accidental one is a test failure with the new value in the log.
 *
 * Binary fixtures for OLD schema versions still belong in Tests/Saves under AGENTS.md
 * section 4 -- schema 2 is the first schema that is ever written, so there is nothing yet to
 * be backward-compatible WITH. The first migration is what creates that directory.
 *
 * IF ONE OF THESE FAILS, the save format changed. Under AGENTS.md section 4 that needs a
 * numbered decision and a migration path. It is not a constant to quietly update.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainPersistenceGoldenTest,
	"TerrainCore.Persistence.Format.Golden",
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

namespace
{
	/** Pinned BLAKE3-256 of each fixture's bytes. An empty string means "not yet pinned". */
	struct FTerrainGoldenVectors
	{
		static constexpr const TCHAR* BaseDescriptorObject    = TEXT("399a28150099767d2083590fbeb0b9ca65900306cbda1f881e4167f583003487");
		static constexpr const TCHAR* BaseDescriptorDigest    = TEXT("17da31d31f8d1e2a755027c169ef5a7e57f23760ccb7af78f0cd01a64a7ada1c");
		static constexpr const TCHAR* ChunkPayloadDenseBody   = TEXT("3cc6bd9b8649f66402fba68db319f76e471a89f9a3db9474b846d5a275ad876d");
		static constexpr const TCHAR* ChunkPayloadDenseObject = TEXT("4ca79f312a0063f8f3b7881bc9fd995916514eb2f5d820f8d7e76aff9993fa97");
		static constexpr const TCHAR* ChunkPayloadSparseBody  = TEXT("ebbfb06c3ea87f53ad6f94e6aa3a55da9ab3199cf2abebc29b3740b8909306e1");
		static constexpr const TCHAR* IndexLeafPageBody       = TEXT("45255f0a9fe6883f4491536c76d2470e3096cdcb7ffccf57a5ea0e86745864a1");
		static constexpr const TCHAR* IndexInternalPageBody   = TEXT("1b2f241a0a7585899f5d0a47da5cc070af5f2525a91478886deda5d86c595a3c");
		static constexpr const TCHAR* CheckpointBody          = TEXT("c84155f57dbaddefe8248e4b76087553c8f82914b60b3a8b3e88029315e790aa");
		static constexpr const TCHAR* RootSlotFile            = TEXT("deee08e4c6f275fae35b304cf9a81f9a924a22f21f6b7e60bee4754619751e9f");
		static constexpr const TCHAR* AnchorSlotFile          = TEXT("e323ec21984f77c4ec374dee78b729c51ad8d042c3586b5ca2b4e585cef51647");
		static constexpr const TCHAR* SegmentHeaderBody       = TEXT("f40a062cd3b26337a32528d3c433057f14be49ec3eb3c4f0e4fa9aace8041283");
		static constexpr const TCHAR* CommitRecordFrame       = TEXT("a9b6d7a6c707261900c685697aeced2062b08a596651f645350a62846010aa71");
		static constexpr const TCHAR* CommitRecordDigest      = TEXT("26511ffbf13fb890974e50962c5b474d591ff3a4d9aca91c199b4c5d5ee2926a");
		static constexpr const TCHAR* CommitIntentDigest      = TEXT("27adad15dbbfbb6b88e637609ebaa3d21497de2baf668cd229762eeb472358e2");
		static constexpr const TCHAR* SealRecordFrame         = TEXT("c67d13f3a5e9d7b843909a2981b92c1872376c00c7611bb72af6b641f0618b93");
	};

	/** Pinned WorldTag for the fixture identity: XXH3-64(WorldId || StoreEpoch). */
	constexpr uint64 TerrainGoldenWorldTag = 0xf7209e61ab62b938ull;
}

bool FTerrainPersistenceGoldenTest::RunTest(const FString& Parameters)
{
	using namespace TerrainPersistTest;

	const FTerrainPersistIdentity Identity = MakeIdentity();

	auto Check = [this](const TCHAR* Name, TArrayView<const uint8> Bytes, const TCHAR* Expected)
	{
		const FString Actual = TerrainPersistDigestToHex(TerrainPersistDigest(Bytes));
		AddInfo(FString::Printf(TEXT("GOLDEN %s = %s (%d bytes)"), Name, *Actual, Bytes.Num()));

		if (FCString::Strlen(Expected) == 0)
		{
			AddError(FString::Printf(
				TEXT("Golden vector '%s' is not pinned. Pin the value printed above."), Name));
			return;
		}
		TestEqual(FString::Printf(TEXT("Golden %s"), Name), Actual, FString(Expected));
	};

	// --- 1. base descriptor, including its self-referential BaseDigest --------------------
	{
		TArray<uint8> Object;
		FTerrainDigest BaseDigest;
		TerrainPersistEncodeBaseDescriptorObject(
			MakeBaseDescriptor(), Identity.World, Identity.Epoch, Object, BaseDigest);

		Check(TEXT("BaseDescriptorObject"), Object, FTerrainGoldenVectors::BaseDescriptorObject);
		Check(TEXT("BaseDescriptorDigest"),
			TArrayView<const uint8>(BaseDigest.Bytes, TerrainPersistDigestSize),
			FTerrainGoldenVectors::BaseDescriptorDigest);
	}

	// --- 2. Dense chunk payload: the 131,072-byte transfer layout, re-homed on disk -------
	{
		TArray<int16>  Densities; Densities.AddZeroed(TerrainChunkSampleCount);
		TArray<uint16> Materials; Materials.AddZeroed(TerrainChunkSampleCount);
		for (int32 Index = 0; Index < TerrainChunkSampleCount; ++Index)
		{
			Densities[Index] = static_cast<int16>((Index * 37) % 65536 - 32768);
			Materials[Index] = static_cast<uint16>(Index % 8);
		}

		FTerrainChunkPayloadRecord Record;
		Record.Key              = FTerrainChunkKey(-3, 12, -700);
		Record.Rev              = 41;
		Record.LastOpSeq        = 900;
		Record.Encoding         = ETerrainRegionEncoding::Dense;
		Record.GeneratorVersion = 7;
		Record.ValueConfig      = 1;
		TerrainPersistBuildDense(Densities, Materials, Record.Dense);

		TArray<uint8> Body;
		TerrainPersistEncodeChunkPayloadBody(Record, Body);
		Check(TEXT("ChunkPayloadDenseBody"), Body, FTerrainGoldenVectors::ChunkPayloadDenseBody);

		TArray<uint8> Object;
		TerrainPersistEncodeObject(ETerrainPersistObjectType::ChunkPayload, Identity, Body, Object);
		Check(TEXT("ChunkPayloadDenseObject"), Object, FTerrainGoldenVectors::ChunkPayloadDenseObject);
	}

	// --- 3. SparseDiff chunk payload -------------------------------------------------------
	{
		FTerrainChunkPayloadRecord Record;
		Record.Key              = FTerrainChunkKey(0, -1, 2);
		Record.Rev              = 3;
		Record.LastOpSeq        = 77;
		Record.Encoding         = ETerrainRegionEncoding::SparseDiff;
		Record.GeneratorVersion = 7;
		Record.ValueConfig      = 1;
		for (int32 Index = 0; Index < 16; ++Index)
		{
			FTerrainChunkSample Sample;
			Sample.LocalIndex = static_cast<uint16>(Index * 1999);
			Sample.Density    = static_cast<int16>(-32768 + Index * 4096);
			Sample.MaterialId = static_cast<uint16>(Index % 8);
			Record.Sparse.Add(Sample);
		}

		TArray<uint8> Body;
		TerrainPersistEncodeChunkPayloadBody(Record, Body);
		Check(TEXT("ChunkPayloadSparseBody"), Body, FTerrainGoldenVectors::ChunkPayloadSparseBody);
	}

	// --- 4. index pages, leaf and internal --------------------------------------------------
	{
		FTerrainIndexPage Leaf;
		Leaf.Depth = TerrainIndexLeafDepth;
		Leaf.bLeaf = true;
		for (int32 Index = 0; Index < TerrainIndexLeafDepth; ++Index)
		{
			Leaf.KeyPrefix[Index] = static_cast<uint8>(Index + 1);
		}
		for (int32 Index = 0; Index < 4; ++Index)
		{
			FTerrainIndexLeafEntry Entry;
			Entry.ByteValue      = static_cast<uint8>(Index * 17);
			Entry.Value.Encoding = Index == 3 ? ETerrainRegionEncoding::Empty : ETerrainRegionEncoding::Dense;
			Entry.Value.Rev      = static_cast<FTerrainRev>(Index + 1);
			Entry.Value.LastOpSeq = static_cast<FTerrainOpSeq>(100 + Index);
			if (Entry.Value.Encoding != ETerrainRegionEncoding::Empty)
			{
				Entry.Value.PayloadLength = 131200;
				Entry.Value.PayloadDigest = MakeDigest(static_cast<uint8>(0x20 + Index));
			}
			Leaf.Leaves.Add(Entry);
		}

		TArray<uint8> Body;
		TerrainIndexEncodePageBody(Leaf, Body);
		Check(TEXT("IndexLeafPageBody"), Body, FTerrainGoldenVectors::IndexLeafPageBody);

		FTerrainIndexPage Internal;
		Internal.Depth = 2;
		Internal.bLeaf = false;
		Internal.KeyPrefix[0] = 0x80;
		Internal.KeyPrefix[1] = 0x00;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FTerrainIndexInternalEntry Entry;
			Entry.ByteValue   = static_cast<uint8>(Index * 64);
			Entry.ChildDigest = MakeDigest(static_cast<uint8>(0x30 + Index));
			Entry.ChildLength = static_cast<uint32>(149 + Index);
			Internal.Internal.Add(Entry);
		}

		TArray<uint8> InternalBody;
		TerrainIndexEncodePageBody(Internal, InternalBody);
		Check(TEXT("IndexInternalPageBody"), InternalBody, FTerrainGoldenVectors::IndexInternalPageBody);
	}

	// --- 5. checkpoint descriptor, both slot files, segment header ---------------------------
	{
		FTerrainCheckpointDescriptor Descriptor;
		Descriptor.G                 = 4096;
		Descriptor.Generation        = 17;
		Descriptor.CreatedUtcMillis  = 1789412345678LL;
		Descriptor.bHasRootPage      = true;
		Descriptor.RootPageLength    = 512;
		Descriptor.RootPageDigest    = MakeDigest(0x77);
		Descriptor.LeafKeyCount      = 900;
		Descriptor.TotalPayloadBytes = 123456789;

		TArray<uint8> Body;
		TerrainPersistEncodeCheckpointBody(Descriptor, Body);
		Check(TEXT("CheckpointDescriptorBody"), Body, FTerrainGoldenVectors::CheckpointBody);

		FTerrainRootSlot Root;
		Root.Generation         = 17;
		Root.G                  = 4096;
		Root.DescriptorDigest   = MakeDigest(0x55);
		Root.DescriptorLength   = 176;
		Root.PublishedUtcMillis = 1789412345678LL;

		TArray<uint8> RootBody;
		TerrainPersistEncodeRootSlotBody(Root, RootBody);
		TArray<uint8> Slot;
		TerrainPersistEncodeSlot(ETerrainPersistObjectType::RootSlot, Identity, RootBody, Slot);
		Check(TEXT("RootSlotFile"), Slot, FTerrainGoldenVectors::RootSlotFile);

		FTerrainJournalAnchor Anchor;
		Anchor.AnchorGeneration        = 5;
		Anchor.ActiveSegmentId         = 9;
		Anchor.ActiveSegmentFirstOpSeq = 401;
		Anchor.PredecessorSealDigest   = MakeDigest(0x88);
		Anchor.PredecessorSegmentId    = 8;
		Anchor.PredecessorLastOpSeq    = 400;
		Anchor.PublishedUtcMillis      = 1789412345678LL;
		Anchor.bHasPredecessor         = true;

		TArray<uint8> AnchorBody;
		TerrainPersistEncodeAnchorBody(Anchor, AnchorBody);
		TArray<uint8> AnchorSlot;
		TerrainPersistEncodeSlot(ETerrainPersistObjectType::JournalAnchorSlot, Identity, AnchorBody, AnchorSlot);
		Check(TEXT("AnchorSlotFile"), AnchorSlot, FTerrainGoldenVectors::AnchorSlotFile);

		FTerrainJournalSegmentHeader Segment;
		Segment.SegmentId             = 9;
		Segment.FirstOpSeq            = 401;
		Segment.PredecessorSegmentId  = 8;
		Segment.PredecessorSealDigest = MakeDigest(0x88);
		Segment.CreatedUtcMillis      = 1789412345678LL;
		Segment.bHasPredecessor       = true;

		TArray<uint8> SegmentBody;
		TerrainPersistEncodeSegmentHeaderBody(Segment, SegmentBody);
		Check(TEXT("SegmentHeaderBody"), SegmentBody, FTerrainGoldenVectors::SegmentHeaderBody);
	}

	// --- 6. journal records, the record digest and the canonical intent digest ---------------
	{
		const FTerrainJournalCommitRecord Commit = MakeCommitRecord();

		TArray<uint8> Frame;
		TerrainPersistEncodeCommitRecord(Commit, Frame);
		Check(TEXT("CommitRecordFrame"), Frame, FTerrainGoldenVectors::CommitRecordFrame);

		FTerrainDigest RecordDigest;
		TerrainPersistComputeRecordDigest(Frame, RecordDigest);
		Check(TEXT("CommitRecordDigest"),
			TArrayView<const uint8>(RecordDigest.Bytes, TerrainPersistDigestSize),
			FTerrainGoldenVectors::CommitRecordDigest);
		Check(TEXT("CommitIntentDigest"),
			TArrayView<const uint8>(Commit.IntentDigest.Bytes, TerrainPersistDigestSize),
			FTerrainGoldenVectors::CommitIntentDigest);

		FTerrainJournalSealRecord Seal;
		Seal.WorldTag          = Commit.WorldTag;
		Seal.LastOpSeq         = 5;
		Seal.CommitRecordCount = 5;
		Seal.RecordsDigest     = MakeDigest(0x66);
		Seal.SealedUtcMillis   = 1789412399999LL;

		TArray<uint8> SealFrame;
		TerrainPersistEncodeSealRecord(Seal, SealFrame);
		Check(TEXT("SealRecordFrame"), SealFrame, FTerrainGoldenVectors::SealRecordFrame);
	}

	// --- 7. the WorldTag splice check --------------------------------------------------------
	{
		const uint64 Tag = TerrainPersistWorldTag(Identity.World, Identity.Epoch);
		AddInfo(FString::Printf(TEXT("GOLDEN WorldTag = 0x%016llx"), Tag));
		TestEqual(TEXT("Golden WorldTag"), Tag, TerrainGoldenWorldTag);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

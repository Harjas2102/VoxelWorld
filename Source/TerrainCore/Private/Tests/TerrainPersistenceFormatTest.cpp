// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "Misc/AutomationTest.h"
#include "TerrainPersistenceFormat.h"
#include "TerrainPersistenceRecords.h"
#include "TerrainPersistenceIndex.h"
#include "TerrainPersistenceFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The schema-2 format tests -- Docs/proposals/P-004 section 13.
 *
 * These are the authority on the format, in the same way TerrainCore.Op.Codec.RoundTrip is the
 * authority on the 58-byte op. Every size in P-004's tables is MEASURED here and logged as a
 * number, not asserted in a comment; every corrupt fixture asserts WHICH defence fired, so a
 * defence cannot quietly stop running because an earlier check started catching its case.
 *
 * Headless: no engine world, no plugin, no file system (ARCHITECTURE.md section 6.1).
 */

// ==== Sizes =============================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainPersistenceSizesTest,
	"TerrainCore.Persistence.Format.Sizes",
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

bool FTerrainPersistenceSizesTest::RunTest(const FString& Parameters)
{
	using namespace TerrainPersistTest;

	const FTerrainPersistIdentity Identity = MakeIdentity();

	// --- the object header, measured from the encoder -----------------------------------
	{
		const uint8 Body[1] = { 0 };
		TArray<uint8> Object;
		TerrainPersistEncodeObject(ETerrainPersistObjectType::ChunkPayload, Identity,
			TArrayView<const uint8>(Body, 1), Object);

		AddInfo(FString::Printf(TEXT("Object header measured %d bytes (P-004 section 3 fixes it at 96)"),
			Object.Num() - 1));
		TestEqual(TEXT("Object header is exactly 96 bytes"), Object.Num() - 1, 96);
		TestEqual(TEXT("TerrainPersistObjectHeaderSize agrees with the encoder"),
			TerrainPersistObjectHeaderSize, 96);
	}

	// --- base descriptor fixed prefix ---------------------------------------------------
	{
		FTerrainBaseDescriptor Base = MakeBaseDescriptor();
		Base.GeneratorName.Empty();
		Base.BackendName.Empty();

		TArray<uint8> Body;
		TestEqual(TEXT("Empty-name base descriptor encodes"),
			TerrainPersistEncodeBaseDescriptorBody(Base, Body), ETerrainPersistError::None);

		AddInfo(FString::Printf(TEXT("Base descriptor fixed prefix measured %d bytes (P-004 section 4: 122)"),
			Body.Num()));
		TestEqual(TEXT("Base descriptor prefix is 122 bytes"), Body.Num(), 122);
	}

	// --- chunk payload ------------------------------------------------------------------
	{
		FTerrainChunkPayloadRecord Record;
		Record.Key      = FTerrainChunkKey(1, 2, 3);
		Record.Encoding = ETerrainRegionEncoding::SparseDiff;
		FTerrainChunkSample Sample;
		Sample.LocalIndex = 0;
		Record.Sparse.Add(Sample);

		TArray<uint8> Body;
		TestEqual(TEXT("One-sample SparseDiff encodes"),
			TerrainPersistEncodeChunkPayloadBody(Record, Body), ETerrainPersistError::None);
		AddInfo(FString::Printf(TEXT("Chunk payload prefix measured %d bytes (P-004 section 5: 32 + 4 + 6)"),
			Body.Num()));
		TestEqual(TEXT("32-byte prefix + 4-byte count + one 6-byte sample"), Body.Num(), 42);

		TArray<int16>  Densities; Densities.AddZeroed(TerrainChunkSampleCount);
		TArray<uint16> Materials; Materials.AddZeroed(TerrainChunkSampleCount);
		FTerrainChunkPayloadRecord DenseRecord;
		DenseRecord.Key      = FTerrainChunkKey(1, 2, 3);
		DenseRecord.Encoding = ETerrainRegionEncoding::Dense;
		TestTrue(TEXT("Dense buffer builds"),
			TerrainPersistBuildDense(Densities, Materials, DenseRecord.Dense));
		TestEqual(TEXT("Dense buffer is 131072 bytes"), DenseRecord.Dense.Num(), 131072);

		TArray<uint8> DenseBody;
		TestEqual(TEXT("Dense chunk payload encodes"),
			TerrainPersistEncodeChunkPayloadBody(DenseRecord, DenseBody), ETerrainPersistError::None);
		AddInfo(FString::Printf(TEXT("Dense chunk payload body measured %d bytes (P-004 section 5.1: 131104)"),
			DenseBody.Num()));
		TestEqual(TEXT("Dense chunk payload body is 131104 bytes"), DenseBody.Num(), 131104);
		TestTrue(TEXT("Dense body is within its cap"), DenseBody.Num() <= TerrainPersistMaxChunkPayloadBody);
	}

	// --- index pages --------------------------------------------------------------------
	{
		FTerrainIndexPage Internal;
		Internal.Depth = 0;
		Internal.bLeaf = false;
		FTerrainIndexInternalEntry Child;
		Child.ByteValue   = 7;
		Child.ChildDigest = MakeDigest(0x22);
		Child.ChildLength = 200;
		Internal.Internal.Add(Child);

		TArray<uint8> Body;
		TestEqual(TEXT("Internal page encodes"),
			TerrainIndexEncodePageBody(Internal, Body), ETerrainPersistError::None);
		AddInfo(FString::Printf(TEXT("Index page header + one internal entry measured %d bytes (16 + 37)"),
			Body.Num()));
		TestEqual(TEXT("Page header is 16 and an internal entry is 37"), Body.Num(), 53);

		FTerrainIndexPage Leaf;
		Leaf.Depth = TerrainIndexLeafDepth;
		Leaf.bLeaf = true;
		FTerrainIndexLeafEntry LeafEntry;
		LeafEntry.ByteValue           = 3;
		LeafEntry.Value.Encoding      = ETerrainRegionEncoding::Dense;
		LeafEntry.Value.Rev           = 5;
		LeafEntry.Value.LastOpSeq     = 9;
		LeafEntry.Value.PayloadLength = 131200;
		LeafEntry.Value.PayloadDigest = MakeDigest(0x33);
		Leaf.Leaves.Add(LeafEntry);

		TArray<uint8> LeafBody;
		TestEqual(TEXT("Leaf page encodes"),
			TerrainIndexEncodePageBody(Leaf, LeafBody), ETerrainPersistError::None);
		AddInfo(FString::Printf(TEXT("Index page header + one leaf entry measured %d bytes (16 + 50)"),
			LeafBody.Num()));
		TestEqual(TEXT("A leaf entry is 50 bytes"), LeafBody.Num(), 66);

		// The maxima P-004 section 6.2 quotes, computed rather than trusted.
		const int32 MaxInternal = 16 + 256 * 37;
		const int32 MaxLeaf     = 16 + 256 * 50;
		AddInfo(FString::Printf(TEXT("Maximum page bodies: internal %d, leaf %d, cap %d"),
			MaxInternal, MaxLeaf, TerrainPersistMaxIndexPageBody));
		TestEqual(TEXT("Maximum internal page body is 9488"), MaxInternal, 9488);
		TestEqual(TEXT("Maximum leaf page body is 12816"), MaxLeaf, 12816);
		TestTrue(TEXT("Both fit the 32 KiB page cap"), MaxLeaf <= TerrainPersistMaxIndexPageBody);
	}

	// --- descriptor, slots, segment header ----------------------------------------------
	{
		FTerrainCheckpointDescriptor Descriptor;
		Descriptor.G            = 100;
		Descriptor.Generation   = 4;
		Descriptor.bHasRootPage = true;
		Descriptor.RootPageLength = 149;
		Descriptor.RootPageDigest = MakeDigest(0x44);

		TArray<uint8> Body;
		TestEqual(TEXT("Checkpoint descriptor encodes"),
			TerrainPersistEncodeCheckpointBody(Descriptor, Body), ETerrainPersistError::None);
		AddInfo(FString::Printf(TEXT("Checkpoint descriptor body measured %d bytes (P-004 section 7: 80)"),
			Body.Num()));
		TestEqual(TEXT("Checkpoint descriptor body is 80 bytes"), Body.Num(), 80);

		FTerrainRootSlot Root;
		Root.Generation       = 4;
		Root.G                = 100;
		Root.DescriptorDigest = MakeDigest(0x55);
		Root.DescriptorLength = 176;

		TArray<uint8> RootBody;
		TestEqual(TEXT("Root slot body encodes"),
			TerrainPersistEncodeRootSlotBody(Root, RootBody), ETerrainPersistError::None);
		AddInfo(FString::Printf(TEXT("Root slot body measured %d bytes (P-004 section 8: 4000)"),
			RootBody.Num()));
		TestEqual(TEXT("Root slot body is 4000 bytes"), RootBody.Num(), 4000);

		TArray<uint8> Slot;
		TestTrue(TEXT("Root slot wraps into a 4096-byte file image"),
			TerrainPersistEncodeSlot(ETerrainPersistObjectType::RootSlot, Identity, RootBody, Slot));
		AddInfo(FString::Printf(TEXT("Root slot file image measured %d bytes (P-004 section 8: 4096)"),
			Slot.Num()));
		TestEqual(TEXT("A slot file is exactly 4096 bytes"), Slot.Num(), TerrainPersistSlotSize);

		FTerrainJournalSegmentHeader Segment;
		Segment.SegmentId  = 1;
		Segment.FirstOpSeq = 1;

		TArray<uint8> SegmentBody;
		TestEqual(TEXT("Segment header encodes"),
			TerrainPersistEncodeSegmentHeaderBody(Segment, SegmentBody), ETerrainPersistError::None);
		AddInfo(FString::Printf(TEXT("Journal segment header body measured %d bytes (P-004 section 9.1: 72)"),
			SegmentBody.Num()));
		TestEqual(TEXT("Segment header body is 72 bytes"), SegmentBody.Num(), 72);

		FTerrainJournalAnchor Anchor;
		Anchor.AnchorGeneration        = 1;
		Anchor.ActiveSegmentId         = 1;
		Anchor.ActiveSegmentFirstOpSeq = 1;

		TArray<uint8> AnchorBody;
		TestEqual(TEXT("Anchor body encodes"),
			TerrainPersistEncodeAnchorBody(Anchor, AnchorBody), ETerrainPersistError::None);
		TestEqual(TEXT("Anchor slot body is 4000 bytes"), AnchorBody.Num(), 4000);
	}

	// --- journal records ----------------------------------------------------------------
	{
		FTerrainJournalCommitRecord Record = MakeCommitRecord();
		Record.Physical.Reset();
		Record.ChangedKeys.Reset();

		TArray<uint8> Frame;
		TestEqual(TEXT("Minimal commit record encodes"),
			TerrainPersistEncodeCommitRecord(Record, Frame), ETerrainPersistError::None);
		AddInfo(FString::Printf(TEXT("Minimal commit record measured %d bytes (P-004 sections 9.2/9.3: 20 + 140)"),
			Frame.Num()));
		TestEqual(TEXT("Frame overhead 20 + commit prefix 140"), Frame.Num(), 160);

		FTerrainJournalSealRecord Seal;
		Seal.WorldTag      = Record.WorldTag;
		Seal.LastOpSeq     = 1;
		Seal.RecordsDigest = MakeDigest(0x66);

		TArray<uint8> SealFrame;
		TestEqual(TEXT("Seal record encodes"),
			TerrainPersistEncodeSealRecord(Seal, SealFrame), ETerrainPersistError::None);
		AddInfo(FString::Printf(TEXT("Seal record measured %d bytes (20 + 64)"), SealFrame.Num()));
		TestEqual(TEXT("Frame overhead 20 + seal body 64"), SealFrame.Num(), 84);

		// P-004 section 9.3's worst case, arithmetic rather than allocated.
		const int32 WorstCase = 20 + 140
			+ TerrainPersistMaxPhysicalEntriesPerRecord * TerrainPersistPhysicalEntryBytes
			+ TerrainPersistMaxChangedKeysPerRecord     * TerrainPersistChangedKeyEntryBytes
			+ TerrainPersistMaxEconomyDeltasPerRecord   * TerrainPersistEconomyDeltaBytes;
		AddInfo(FString::Printf(TEXT("Worst-case commit record is %d bytes against a %d byte cap"),
			WorstCase, TerrainPersistMaxJournalRecordBytes));
		TestEqual(TEXT("Worst-case commit record is 84768 bytes"), WorstCase, 84768);
		TestTrue(TEXT("Worst case fits the record cap"), WorstCase <= TerrainPersistMaxJournalRecordBytes);

		// P-004 section 10.1's bytes-per-edit figure, in the log where a reader can see it.
		const int32 TypicalEdit = 20 + 140 + 3 * TerrainPersistPhysicalEntryBytes
			+ 8 * TerrainPersistChangedKeyEntryBytes;
		AddInfo(FString::Printf(
			TEXT("Computed journal bytes for a radius-4 dig over 8 chunks with 3 materials: %d"),
			TypicalEdit));
		TestEqual(TEXT("P-004 section 10.1's 350-byte figure"), TypicalEdit, 350);
	}

	// --- the SparseDiff/Dense break-even, computed --------------------------------------
	{
		const int32 DenseBody = TerrainPersistChunkPayloadPrefix + TerrainPersistDenseBytes;
		int32 BreakEven = 0;
		while (TerrainPersistChunkPayloadPrefix + 4 + (BreakEven + 1) * TerrainPersistSparseEntryBytes < DenseBody)
		{
			++BreakEven;
		}
		AddInfo(FString::Printf(TEXT("SparseDiff is smaller up to N = %d changed samples"), BreakEven));
		TestEqual(TEXT("P-004 section 5.3's break-even is 21844"), BreakEven, TerrainPersistSparseBreakEven);
	}

	return true;
}

// ==== RoundTrip =========================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainPersistenceRoundTripTest,
	"TerrainCore.Persistence.Format.RoundTrip",
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

bool FTerrainPersistenceRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace TerrainPersistTest;

	const FTerrainPersistIdentity Identity = MakeIdentity();

	// --- base descriptor, including the self-referential digest -------------------------
	{
		const FTerrainBaseDescriptor Base = MakeBaseDescriptor();

		TArray<uint8> Object;
		FTerrainDigest BaseDigest;
		TestEqual(TEXT("Base descriptor object encodes"),
			TerrainPersistEncodeBaseDescriptorObject(Base, Identity.World, Identity.Epoch, Object, BaseDigest),
			ETerrainPersistError::None);

		FTerrainPersistIdentity Decoded;
		FTerrainBaseDescriptor Back;
		TestEqual(TEXT("Base descriptor object decodes"),
			TerrainPersistDecodeBaseDescriptorObject(Object, Decoded, Back), ETerrainPersistError::None);

		TestEqual(TEXT("Seed survives"), Back.Seed, Base.Seed);
		TestEqual(TEXT("Generator version survives"), Back.GeneratorVersion, Base.GeneratorVersion);
		TestEqual(TEXT("Kernel version survives"), Back.BackendKernelVersion, Base.BackendKernelVersion);
		TestTrue (TEXT("Params digest survives"), Back.GeneratorParamsDigest == Base.GeneratorParamsDigest);
		TestEqual(TEXT("Negative origin survives exactly"),
			Back.OriginWorldMicrometres[0], Base.OriginWorldMicrometres[0]);
		TestEqual(TEXT("Voxel size survives exactly"), Back.VoxelSizeMicrometres, Base.VoxelSizeMicrometres);
		TestEqual(TEXT("Chunk size survives"), Back.ChunkSizeVox, Base.ChunkSizeVox);
		TestTrue (TEXT("World bounds survive"), Back.WorldBoundsVox == Base.WorldBoundsVox);
		TestEqual(TEXT("Value config survives"), (int32)Back.ValueConfig, (int32)Base.ValueConfig);
		TestEqual(TEXT("Catalog version survives"), (int32)Back.MaterialCatalogVersion, (int32)Base.MaterialCatalogVersion);
		TestEqual(TEXT("Generator name survives"), Back.GeneratorName, Base.GeneratorName);
		TestEqual(TEXT("Backend name survives"), Back.BackendName, Base.BackendName);
		TestTrue (TEXT("Decoded identity carries the self-referential base digest"),
			Decoded.BaseDigest == BaseDigest);

		// Byte identity: re-encoding the decoded value reproduces the original object exactly.
		TArray<uint8> Again;
		FTerrainDigest AgainDigest;
		TerrainPersistEncodeBaseDescriptorObject(Back, Identity.World, Identity.Epoch, Again, AgainDigest);
		TestTrue(TEXT("Base descriptor re-encodes byte-identically"), Again == Object);
	}

	// --- chunk payload: Dense, SparseDiff, negative coordinates -------------------------
	{
		TArray<int16>  Densities; Densities.AddZeroed(TerrainChunkSampleCount);
		TArray<uint16> Materials; Materials.AddZeroed(TerrainChunkSampleCount);
		for (int32 Index = 0; Index < TerrainChunkSampleCount; ++Index)
		{
			Densities[Index] = static_cast<int16>((Index % 7) - 3);
			Materials[Index] = static_cast<uint16>(Index % 8);
		}

		FTerrainChunkPayloadRecord Record;
		Record.Key              = FTerrainChunkKey(-9, 0, -1);
		Record.Rev              = 12;
		Record.LastOpSeq        = 345;
		Record.Encoding         = ETerrainRegionEncoding::Dense;
		Record.GeneratorVersion = 7;
		Record.ValueConfig      = 1;
		TestTrue(TEXT("Dense buffer builds"), TerrainPersistBuildDense(Densities, Materials, Record.Dense));

		TArray<uint8> Body;
		TestEqual(TEXT("Dense payload encodes"),
			TerrainPersistEncodeChunkPayloadBody(Record, Body), ETerrainPersistError::None);

		FTerrainChunkPayloadRecord Back;
		TestEqual(TEXT("Dense payload decodes"),
			TerrainPersistDecodeChunkPayloadBody(Body, 7, 1, Back), ETerrainPersistError::None);
		TestTrue (TEXT("Negative chunk key survives"), Back.Key == Record.Key);
		TestEqual(TEXT("Rev survives"), Back.Rev, Record.Rev);
		TestEqual(TEXT("LastOpSeq survives"), Back.LastOpSeq, Record.LastOpSeq);
		TestTrue (TEXT("Dense bytes survive"), Back.Dense == Record.Dense);

		// Samples read back through the accessors, which is the layout claim that matters.
		TestEqual(TEXT("Dense sample 0 density"), (int32)TerrainPersistDenseDensityAt(Back.Dense, 0), (int32)Densities[0]);
		TestEqual(TEXT("Dense sample 5000 density"), (int32)TerrainPersistDenseDensityAt(Back.Dense, 5000), (int32)Densities[5000]);
		TestEqual(TEXT("Dense sample 5000 material"), (int32)TerrainPersistDenseMaterialAt(Back.Dense, 5000), (int32)Materials[5000]);

		FTerrainChunkPayloadRecord Sparse;
		Sparse.Key              = FTerrainChunkKey(-9, 0, -1);
		Sparse.Rev              = 12;
		Sparse.LastOpSeq        = 345;
		Sparse.Encoding         = ETerrainRegionEncoding::SparseDiff;
		Sparse.GeneratorVersion = 7;
		Sparse.ValueConfig      = 1;
		for (int32 Index = 0; Index < 40; ++Index)
		{
			FTerrainChunkSample Sample;
			Sample.LocalIndex = static_cast<uint16>(Index * 811);
			Sample.Density    = static_cast<int16>(-1000 + Index);
			Sample.MaterialId = static_cast<uint16>(Index % 8);
			Sparse.Sparse.Add(Sample);
		}

		TArray<uint8> SparseBody;
		TestEqual(TEXT("SparseDiff encodes"),
			TerrainPersistEncodeChunkPayloadBody(Sparse, SparseBody), ETerrainPersistError::None);
		TestEqual(TEXT("SparseDiff body is 32 + 4 + 6N"), SparseBody.Num(), 32 + 4 + 40 * 6);

		FTerrainChunkPayloadRecord SparseBack;
		TestEqual(TEXT("SparseDiff decodes"),
			TerrainPersistDecodeChunkPayloadBody(SparseBody, 7, 1, SparseBack), ETerrainPersistError::None);
		TestEqual(TEXT("Sample count survives"), SparseBack.Sparse.Num(), 40);
		TestEqual(TEXT("Sample 17 index survives"), (int32)SparseBack.Sparse[17].LocalIndex, 17 * 811);
		TestEqual(TEXT("Negative density survives"), (int32)SparseBack.Sparse[17].Density, -1000 + 17);

		TArray<uint8> SparseAgain;
		TerrainPersistEncodeChunkPayloadBody(SparseBack, SparseAgain);
		TestTrue(TEXT("SparseDiff re-encodes byte-identically"), SparseAgain == SparseBody);
	}

	// --- the encoding-choice rule -------------------------------------------------------
	{
		TArray<int16>  Densities; Densities.AddZeroed(TerrainChunkSampleCount);
		TArray<uint16> Materials; Materials.AddZeroed(TerrainChunkSampleCount);
		TArray<uint8> BaseDense;
		TerrainPersistBuildDense(Densities, Materials, BaseDense);

		// Identical to the base: Empty, and no payload object exists.
		{
			FTerrainChunkPayloadRecord Chosen;
			TestEqual(TEXT("Identical chunk chooses an encoding"),
				TerrainPersistChooseChunkEncoding(FTerrainChunkKey(0, 0, 0), 3, 9, 7, 1,
					BaseDense, BaseDense, Chosen), ETerrainPersistError::None);
			TestTrue(TEXT("A chunk equal to the base is Empty"),
				Chosen.Encoding == ETerrainRegionEncoding::Empty);

			TArray<uint8> Body;
			TestEqual(TEXT("Empty is refused as a payload object"),
				TerrainPersistEncodeChunkPayloadBody(Chosen, Body),
				ETerrainPersistError::EncodingNotPermitted);
		}

		// One differing material and no differing density: still not pristine.
		{
			TArray<uint16> Changed = Materials;
			Changed[99] = 4;
			TArray<uint8> Current;
			TerrainPersistBuildDense(Densities, Changed, Current);

			FTerrainChunkPayloadRecord Chosen;
			TerrainPersistChooseChunkEncoding(FTerrainChunkKey(0, 0, 0), 3, 9, 7, 1, Current, BaseDense, Chosen);
			TestTrue(TEXT("A material-only difference is not Empty"),
				Chosen.Encoding == ETerrainRegionEncoding::SparseDiff);
			TestEqual(TEXT("Exactly one sample differs"), Chosen.Sparse.Num(), 1);
			TestEqual(TEXT("The differing sample is the one that changed"),
				(int32)Chosen.Sparse[0].LocalIndex, 99);
		}

		// Past the break-even, Dense wins.
		{
			TArray<int16> Changed = Densities;
			for (int32 Index = 0; Index <= TerrainPersistSparseBreakEven; ++Index)
			{
				Changed[Index] = 1;
			}
			TArray<uint8> Current;
			TerrainPersistBuildDense(Changed, Materials, Current);

			FTerrainChunkPayloadRecord Chosen;
			TerrainPersistChooseChunkEncoding(FTerrainChunkKey(0, 0, 0), 3, 9, 7, 1, Current, BaseDense, Chosen);
			AddInfo(FString::Printf(TEXT("%d differing samples chose %s"),
				TerrainPersistSparseBreakEven + 1,
				Chosen.Encoding == ETerrainRegionEncoding::Dense ? TEXT("Dense") : TEXT("SparseDiff")));
			TestTrue(TEXT("One past the break-even chooses Dense"),
				Chosen.Encoding == ETerrainRegionEncoding::Dense);
			TestEqual(TEXT("Dense choice carries the full buffer"), Chosen.Dense.Num(), TerrainPersistDenseBytes);
			TestEqual(TEXT("Dense choice carries no sparse list"), Chosen.Sparse.Num(), 0);
		}

		// At the break-even, SparseDiff still wins.
		{
			TArray<int16> Changed = Densities;
			for (int32 Index = 0; Index < TerrainPersistSparseBreakEven; ++Index)
			{
				Changed[Index] = 1;
			}
			TArray<uint8> Current;
			TerrainPersistBuildDense(Changed, Materials, Current);

			FTerrainChunkPayloadRecord Chosen;
			TerrainPersistChooseChunkEncoding(FTerrainChunkKey(0, 0, 0), 3, 9, 7, 1, Current, BaseDense, Chosen);
			TestTrue(TEXT("At the break-even SparseDiff is still smaller"),
				Chosen.Encoding == ETerrainRegionEncoding::SparseDiff);
		}
	}

	// --- descriptor, slots, segment header, anchor ---------------------------------------
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
		FTerrainCheckpointDescriptor Back;
		TestEqual(TEXT("Checkpoint descriptor decodes"),
			TerrainPersistDecodeCheckpointBody(Body, Back), ETerrainPersistError::None);
		TestEqual(TEXT("G survives"), Back.G, Descriptor.G);
		TestEqual(TEXT("Generation survives"), Back.Generation, Descriptor.Generation);
		TestTrue (TEXT("HasRootPage survives"), Back.bHasRootPage);
		TestTrue (TEXT("Root digest survives"), Back.RootPageDigest == Descriptor.RootPageDigest);
		TestEqual(TEXT("Advisory counts survive"), Back.TotalPayloadBytes, Descriptor.TotalPayloadBytes);

		// The empty G=0 checkpoint world creation publishes.
		FTerrainCheckpointDescriptor EmptyCheckpoint;
		TArray<uint8> EmptyBody;
		TestEqual(TEXT("Empty G=0 checkpoint encodes"),
			TerrainPersistEncodeCheckpointBody(EmptyCheckpoint, EmptyBody), ETerrainPersistError::None);
		FTerrainCheckpointDescriptor EmptyBack;
		TestEqual(TEXT("Empty G=0 checkpoint decodes"),
			TerrainPersistDecodeCheckpointBody(EmptyBody, EmptyBack), ETerrainPersistError::None);
		TestFalse(TEXT("Empty checkpoint has no root page"), EmptyBack.bHasRootPage);

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
		FTerrainJournalAnchor AnchorBack;
		TestEqual(TEXT("Anchor decodes"),
			TerrainPersistDecodeAnchorBody(AnchorBody, AnchorBack), ETerrainPersistError::None);
		TestEqual(TEXT("Active segment survives"), AnchorBack.ActiveSegmentId, Anchor.ActiveSegmentId);
		TestEqual(TEXT("Predecessor last OpSeq survives"), AnchorBack.PredecessorLastOpSeq, Anchor.PredecessorLastOpSeq);
		TestTrue (TEXT("Predecessor seal digest survives"), AnchorBack.PredecessorSealDigest == Anchor.PredecessorSealDigest);

		TArray<uint8> Slot;
		TestTrue(TEXT("Anchor wraps into a slot"),
			TerrainPersistEncodeSlot(ETerrainPersistObjectType::JournalAnchorSlot, Identity, AnchorBody, Slot));
		FTerrainPersistObjectHeader SlotHeader;
		TArrayView<const uint8> SlotBody;
		TestEqual(TEXT("Anchor slot object decodes"),
			TerrainPersistDecodeObject(Slot, ETerrainPersistObjectType::JournalAnchorSlot, &Identity, SlotHeader, SlotBody),
			ETerrainPersistError::None);
		TestEqual(TEXT("Anchor slot body is the whole 4000 bytes"), SlotBody.Num(), 4000);
	}

	// --- journal commit record, including a zero-change commit ---------------------------
	{
		const FTerrainJournalCommitRecord Record = MakeCommitRecord();

		TArray<uint8> Frame;
		TestEqual(TEXT("Commit record encodes"),
			TerrainPersistEncodeCommitRecord(Record, Frame), ETerrainPersistError::None);

		FTerrainJournalCommitRecord Back;
		TestEqual(TEXT("Commit record decodes"),
			TerrainPersistDecodeCommitRecord(Frame, Record.WorldTag, Back), ETerrainPersistError::None);

		TestEqual(TEXT("OpSeq survives"), Back.Op.OpSeq, Record.Op.OpSeq);
		TestTrue (TEXT("Negative op centre survives"), Back.Op.CentreVox == Record.Op.CentreVox);
		TestEqual(TEXT("Radius survives"), Back.Op.RadiusVoxQ16, Record.Op.RadiusVoxQ16);
		TestEqual(TEXT("RequestId survives"), Back.RequestId, Record.RequestId);
		TestTrue (TEXT("Intent digest survives"), Back.IntentDigest == Record.IntentDigest);
		TestEqual(TEXT("Physical list survives"), Back.Physical.Num(), Record.Physical.Num());
		TestEqual(TEXT("Signed microlitres survive"), Back.Physical[0].MicroLitres, Record.Physical[0].MicroLitres);
		TestEqual(TEXT("Changed key list survives"), Back.ChangedKeys.Num(), 3);
		TestTrue (TEXT("Negative changed key survives"), Back.ChangedKeys[0].Key == FTerrainChunkKey(-2, 0, -2));
		TestTrue (TEXT("Token digest survives"),
			FMemory::Memcmp(Back.TokenDigest, Record.TokenDigest, TerrainPersistTokenDigestBytes) == 0);

		TArray<uint8> Again;
		TerrainPersistEncodeCommitRecord(Back, Again);
		TestTrue(TEXT("Commit record re-encodes byte-identically"), Again == Frame);

		// A successful zero-change op still commits and still consumes a sequence (P-003 2).
		FTerrainJournalCommitRecord ZeroChange = Record;
		ZeroChange.ChangedKeys.Reset();
		TArray<uint8> ZeroFrame;
		TestEqual(TEXT("Zero-change commit encodes"),
			TerrainPersistEncodeCommitRecord(ZeroChange, ZeroFrame), ETerrainPersistError::None);
		FTerrainJournalCommitRecord ZeroBack;
		TestEqual(TEXT("Zero-change commit decodes"),
			TerrainPersistDecodeCommitRecord(ZeroFrame, Record.WorldTag, ZeroBack), ETerrainPersistError::None);
		TestEqual(TEXT("Zero-change commit has an empty changed-key list"), ZeroBack.ChangedKeys.Num(), 0);

		// The record digest is stable and covers the body.
		FTerrainDigest DigestA;
		FTerrainDigest DigestB;
		TestEqual(TEXT("Record digest computes"),
			TerrainPersistComputeRecordDigest(Frame, DigestA), ETerrainPersistError::None);
		TerrainPersistComputeRecordDigest(Again, DigestB);
		TestTrue(TEXT("Identical frames have identical record digests"), DigestA == DigestB);

		// P-004 section 9.6: the intent digest ignores OpSeq.
		FTerrainOp Later = Record.Op;
		Later.OpSeq = 987654;
		const FTerrainDigest IntentLater = TerrainPersistComputeIntentDigest(
			Later, Record.TokenDigest, Record.RequestId, Record.ChildOrdinal, Record.ChildCount);
		TestTrue(TEXT("The intent digest does not depend on the assigned OpSeq"),
			IntentLater == Record.IntentDigest);

		FTerrainOp Different = Record.Op;
		Different.CentreVox.X += 1;
		const FTerrainDigest IntentDifferent = TerrainPersistComputeIntentDigest(
			Different, Record.TokenDigest, Record.RequestId, Record.ChildOrdinal, Record.ChildCount);
		TestFalse(TEXT("A changed intent changes the digest"), IntentDifferent == Record.IntentDigest);
	}

	return true;
}

// ==== Corrupt ===========================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainPersistenceCorruptTest,
	"TerrainCore.Persistence.Format.Corrupt",
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

bool FTerrainPersistenceCorruptTest::RunTest(const FString& Parameters)
{
	using namespace TerrainPersistTest;

	const FTerrainPersistIdentity Identity = MakeIdentity();

	// A valid chunk payload object to damage in named, individual ways.
	FTerrainChunkPayloadRecord Record;
	Record.Key              = FTerrainChunkKey(3, -4, 5);
	Record.Rev              = 2;
	Record.LastOpSeq        = 8;
	Record.Encoding         = ETerrainRegionEncoding::SparseDiff;
	Record.GeneratorVersion = 7;
	Record.ValueConfig      = 1;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FTerrainChunkSample Sample;
		Sample.LocalIndex = static_cast<uint16>(Index * 10);
		Sample.Density    = static_cast<int16>(Index);
		Sample.MaterialId = 4;
		Record.Sparse.Add(Sample);
	}

	TArray<uint8> Body;
	TerrainPersistEncodeChunkPayloadBody(Record, Body);

	TArray<uint8> Valid;
	TerrainPersistEncodeObject(ETerrainPersistObjectType::ChunkPayload, Identity, Body, Valid);

	FTerrainPersistObjectHeader Header;
	TArrayView<const uint8> DecodedBody;

	auto Expect = [&](const TCHAR* Label, const TArray<uint8>& Bytes,
		const FTerrainPersistIdentity* ExpectIdentity, ETerrainPersistError Expected)
	{
		const ETerrainPersistError Actual = TerrainPersistDecodeObject(
			Bytes, ETerrainPersistObjectType::ChunkPayload, ExpectIdentity, Header, DecodedBody);
		TestEqual(FString::Printf(TEXT("%s -> %s"), Label, TerrainPersistErrorName(Expected)), Actual, Expected);
	};

	Expect(TEXT("the undamaged object"), Valid, &Identity, ETerrainPersistError::None);

	{
		TArray<uint8> Short = Valid;
		Short.SetNum(64);
		Expect(TEXT("truncated below the header"), Short, &Identity, ETerrainPersistError::ShortBuffer);
	}
	{
		TArray<uint8> ShortBody = Valid;
		ShortBody.SetNum(Valid.Num() - 1);
		Expect(TEXT("truncated body"), ShortBody, &Identity, ETerrainPersistError::ShortBuffer);
	}
	{
		TArray<uint8> Trailing = Valid;
		Trailing.Add(0);
		Expect(TEXT("one trailing byte"), Trailing, &Identity, ETerrainPersistError::TrailingBytes);
	}
	{
		TArray<uint8> Magic = Valid;
		Magic[0] ^= 0xFF;
		Expect(TEXT("bad magic"), Magic, &Identity, ETerrainPersistError::BadMagic);
	}
	{
		TArray<uint8> Legacy = Valid;
		Legacy[4] = 1;   // schema 1, the withdrawn sketch
		Legacy[5] = 0;
		ResealObject(Legacy);
		Expect(TEXT("schema 1"), Legacy, &Identity, ETerrainPersistError::UnsupportedSchema);
	}
	{
		TArray<uint8> BadSize = Valid;
		BadSize[7] = 97;
		ResealObject(BadSize);
		Expect(TEXT("header size 97"), BadSize, &Identity, ETerrainPersistError::BadHeaderSize);
	}
	{
		TArray<uint8> Flipped = Valid;
		Flipped[10] ^= 0x01;   // a WorldId byte, checksum NOT recomputed
		Expect(TEXT("damaged header byte"), Flipped, &Identity, ETerrainPersistError::HeaderChecksumMismatch);
	}
	{
		TArray<uint8> Flipped = Valid;
		Flipped[TerrainPersistObjectHeaderSize + 4] ^= 0x01;   // a body byte
		Expect(TEXT("damaged body byte"), Flipped, &Identity, ETerrainPersistError::BodyChecksumMismatch);
	}
	{
		TArray<uint8> WrongType = Valid;
		WrongType[6] = 9;   // beyond the last defined type
		ResealObject(WrongType);
		Expect(TEXT("object type 9"), WrongType, &Identity, ETerrainPersistError::UnknownObjectType);
	}
	{
		// A well-formed object of a DIFFERENT valid type, asked for as a chunk payload.
		TArray<uint8> RootBody;
		FTerrainRootSlot Root;
		Root.Generation = 1;
		Root.DescriptorDigest = MakeDigest(0x99);
		Root.DescriptorLength = 176;
		TerrainPersistEncodeRootSlotBody(Root, RootBody);

		TArray<uint8> Slot;
		TerrainPersistEncodeSlot(ETerrainPersistObjectType::RootSlot, Identity, RootBody, Slot);
		Expect(TEXT("a root slot read as a chunk payload"), Slot, &Identity, ETerrainPersistError::UnknownObjectType);
	}
	{
		FTerrainPersistIdentity Other = Identity;
		Other.World.Bytes[0] ^= 0xFF;
		Expect(TEXT("another world's identity"), Valid, &Other, ETerrainPersistError::WorldMismatch);
	}
	{
		FTerrainPersistIdentity Other = Identity;
		Other.Epoch.Bytes[3] ^= 0xFF;
		Expect(TEXT("another store epoch"), Valid, &Other, ETerrainPersistError::EpochMismatch);
	}
	{
		FTerrainPersistIdentity Other = Identity;
		Other.BaseDigest.Bytes[9] ^= 0xFF;
		Expect(TEXT("another base"), Valid, &Other, ETerrainPersistError::BaseMismatch);
	}
	{
		TArray<uint8> Huge = Valid;
		WriteU64At(Huge, 72, 1ull << 40);
		ResealObject(Huge);   // the header checksum must pass so the LENGTH check is what fires
		Expect(TEXT("an absurd declared body length"), Huge, &Identity, ETerrainPersistError::BodyLengthOutOfRange);
	}

	// --- body-level defences -------------------------------------------------------------
	{
		TArray<uint8> Reserved = Body;
		Reserved[30] = 1;   // the chunk payload's reserved uint16
		FTerrainChunkPayloadRecord Out;
		TestEqual(TEXT("a nonzero reserved byte -> ReservedNotZero"),
			TerrainPersistDecodeChunkPayloadBody(Reserved, 7, 1, Out), ETerrainPersistError::ReservedNotZero);
	}
	{
		TArray<uint8> BadEncoding = Body;
		BadEncoding[24] = 3;   // past Empty
		FTerrainChunkPayloadRecord Out;
		TestEqual(TEXT("encoding byte 3 -> FieldOutOfRange"),
			TerrainPersistDecodeChunkPayloadBody(BadEncoding, 7, 1, Out), ETerrainPersistError::FieldOutOfRange);
	}
	{
		TArray<uint8> EmptyEncoding = Body;
		EmptyEncoding[24] = static_cast<uint8>(ETerrainRegionEncoding::Empty);
		FTerrainChunkPayloadRecord Out;
		TestEqual(TEXT("Empty as a payload object -> EncodingNotPermitted"),
			TerrainPersistDecodeChunkPayloadBody(EmptyEncoding, 7, 1, Out),
			ETerrainPersistError::EncodingNotPermitted);
	}
	{
		FTerrainChunkPayloadRecord Out;
		TestEqual(TEXT("a chunk from another generator -> BaseMismatch"),
			TerrainPersistDecodeChunkPayloadBody(Body, 8, 1, Out), ETerrainPersistError::BaseMismatch);
		TestEqual(TEXT("a chunk with another value config -> BaseMismatch"),
			TerrainPersistDecodeChunkPayloadBody(Body, 7, 0, Out), ETerrainPersistError::BaseMismatch);
	}
	{
		FTerrainChunkPayloadRecord Descending = Record;
		Swap(Descending.Sparse[1], Descending.Sparse[2]);
		TArray<uint8> Out;
		TestEqual(TEXT("descending sparse indices -> OrderViolation"),
			TerrainPersistEncodeChunkPayloadBody(Descending, Out), ETerrainPersistError::OrderViolation);

		FTerrainChunkPayloadRecord Duplicate = Record;
		Duplicate.Sparse[2] = Duplicate.Sparse[1];
		TestEqual(TEXT("duplicate sparse indices -> OrderViolation"),
			TerrainPersistEncodeChunkPayloadBody(Duplicate, Out), ETerrainPersistError::OrderViolation);

		FTerrainChunkPayloadRecord Zero = Record;
		Zero.Sparse.Reset();
		TestEqual(TEXT("a zero-sample SparseDiff -> FieldOutOfRange"),
			TerrainPersistEncodeChunkPayloadBody(Zero, Out), ETerrainPersistError::FieldOutOfRange);

		FTerrainChunkPayloadRecord OutOfRange = Record;
		OutOfRange.Sparse[3].LocalIndex = static_cast<uint16>(TerrainChunkSampleCount);
		TestEqual(TEXT("a local index past the chunk -> FieldOutOfRange"),
			TerrainPersistEncodeChunkPayloadBody(OutOfRange, Out), ETerrainPersistError::FieldOutOfRange);
	}

	// --- commit record defences -----------------------------------------------------------
	{
		FTerrainJournalCommitRecord Commit = MakeCommitRecord();
		TArray<uint8> Frame;

		FTerrainJournalCommitRecord Unavailable = Commit;
		Unavailable.PhysicalAvailability = ETerrainPhysicalAvailability::Unavailable;
		TestEqual(TEXT("a measured list flagged Unavailable -> FieldOutOfRange"),
			TerrainPersistEncodeCommitRecord(Unavailable, Frame), ETerrainPersistError::FieldOutOfRange);

		FTerrainJournalCommitRecord Economy = Commit;
		Economy.EconomyKind = ETerrainEconomyKind::NoEconomy;
		Economy.EconomyDeltas.AddDefaulted(1);
		Frame.Reset();
		TestEqual(TEXT("NoEconomy with deltas -> FieldOutOfRange"),
			TerrainPersistEncodeCommitRecord(Economy, Frame), ETerrainPersistError::FieldOutOfRange);

		FTerrainJournalCommitRecord Descending = Commit;
		Swap(Descending.ChangedKeys[0], Descending.ChangedKeys[1]);
		Frame.Reset();
		TestEqual(TEXT("descending changed keys -> OrderViolation"),
			TerrainPersistEncodeCommitRecord(Descending, Frame), ETerrainPersistError::OrderViolation);

		FTerrainJournalCommitRecord Stalled = Commit;
		Stalled.ChangedKeys[0].AfterRev = Stalled.ChangedKeys[0].BeforeRev;
		Frame.Reset();
		TestEqual(TEXT("a changed key whose revision did not advance -> FieldOutOfRange"),
			TerrainPersistEncodeCommitRecord(Stalled, Frame), ETerrainPersistError::FieldOutOfRange);

		FTerrainJournalCommitRecord BadChild = Commit;
		BadChild.ChildOrdinal = 3;
		BadChild.ChildCount   = 3;
		Frame.Reset();
		TestEqual(TEXT("child ordinal past child count -> FieldOutOfRange"),
			TerrainPersistEncodeCommitRecord(BadChild, Frame), ETerrainPersistError::FieldOutOfRange);

		FTerrainJournalCommitRecord TooMany = Commit;
		TooMany.Physical.Reset();
		TooMany.Physical.AddDefaulted(TerrainPersistMaxPhysicalEntriesPerRecord + 1);
		Frame.Reset();
		TestEqual(TEXT("too many physical entries -> CapExceeded"),
			TerrainPersistEncodeCommitRecord(TooMany, Frame), ETerrainPersistError::CapExceeded);

		// The splice check: a valid record, read against a different world tag.
		Frame.Reset();
		TerrainPersistEncodeCommitRecord(Commit, Frame);
		FTerrainJournalCommitRecord Out;
		TestEqual(TEXT("a record spliced from another world -> WorldMismatch"),
			TerrainPersistDecodeCommitRecord(Frame, Commit.WorldTag ^ 0xFFull, Out),
			ETerrainPersistError::WorldMismatch);

		// Frame-level damage.
		TArray<uint8> Damaged = Frame;
		Damaged[Damaged.Num() - 12] ^= 0x01;
		int32 Length = 0;
		ETerrainJournalRecordType Type = ETerrainJournalRecordType::Commit;
		TestEqual(TEXT("a damaged record frame -> BodyChecksumMismatch"),
			TerrainPersistPeekRecordFrame(Damaged, Length, Type), ETerrainPersistError::BodyChecksumMismatch);

		TArray<uint8> BadRecordMagic = Frame;
		BadRecordMagic[0] ^= 0xFF;
		TestEqual(TEXT("bad record magic -> BadMagic"),
			TerrainPersistPeekRecordFrame(BadRecordMagic, Length, Type), ETerrainPersistError::BadMagic);

		TArray<uint8> BadRecordVersion = Frame;
		BadRecordVersion[9] = 1;
		ResealRecord(BadRecordVersion);
		TestEqual(TEXT("record version 1 -> UnsupportedSchema"),
			TerrainPersistPeekRecordFrame(BadRecordVersion, Length, Type), ETerrainPersistError::UnsupportedSchema);

		TArray<uint8> BadRecordType = Frame;
		BadRecordType[8] = 4;
		ResealRecord(BadRecordType);
		TestEqual(TEXT("record type 4 -> FieldOutOfRange"),
			TerrainPersistPeekRecordFrame(BadRecordType, Length, Type), ETerrainPersistError::FieldOutOfRange);

		TArray<uint8> BadRecordReserved = Frame;
		BadRecordReserved[10] = 1;
		ResealRecord(BadRecordReserved);
		TestEqual(TEXT("a nonzero record reserved byte -> ReservedNotZero"),
			TerrainPersistPeekRecordFrame(BadRecordReserved, Length, Type), ETerrainPersistError::ReservedNotZero);

		// A commit frame asked for as a seal.
		FTerrainJournalSealRecord SealOut;
		TestEqual(TEXT("a commit frame read as a seal -> UnknownObjectType"),
			TerrainPersistDecodeSealRecord(Frame, Commit.WorldTag, SealOut), ETerrainPersistError::UnknownObjectType);
	}

	// --- slot and segment-header field rules ----------------------------------------------
	{
		FTerrainJournalSegmentHeader Segment;
		Segment.SegmentId  = 2;
		Segment.FirstOpSeq = 0;   // OpSeq is 1-based
		TArray<uint8> Out;
		TestEqual(TEXT("a segment claiming FirstOpSeq 0 -> FieldOutOfRange"),
			TerrainPersistEncodeSegmentHeaderBody(Segment, Out), ETerrainPersistError::FieldOutOfRange);

		Segment.FirstOpSeq = 5;
		Segment.SegmentId  = 0;   // the anchor uses 0 to mean "no predecessor"
		TestEqual(TEXT("a segment claiming SegmentId 0 -> FieldOutOfRange"),
			TerrainPersistEncodeSegmentHeaderBody(Segment, Out), ETerrainPersistError::FieldOutOfRange);

		Segment.SegmentId            = 2;
		Segment.bHasPredecessor      = false;
		Segment.PredecessorSegmentId = 1;   // contradicts the flag
		TestEqual(TEXT("a predecessor ID without the flag -> FieldOutOfRange"),
			TerrainPersistEncodeSegmentHeaderBody(Segment, Out), ETerrainPersistError::FieldOutOfRange);

		Segment.bHasPredecessor    = true;
		Segment.PredecessorSealDigest = FTerrainDigest();   // zero digest contradicts the flag
		TestEqual(TEXT("a flagged predecessor with no seal digest -> FieldOutOfRange"),
			TerrainPersistEncodeSegmentHeaderBody(Segment, Out), ETerrainPersistError::FieldOutOfRange);
	}
	{
		FTerrainRootSlot Root;
		Root.DescriptorDigest = MakeDigest(0xAA);
		Root.DescriptorLength = 0;   // a root that names nothing
		TArray<uint8> Out;
		TestEqual(TEXT("a root slot naming a zero-length descriptor -> FieldOutOfRange"),
			TerrainPersistEncodeRootSlotBody(Root, Out), ETerrainPersistError::FieldOutOfRange);

		Root.DescriptorLength = 176;
		TestEqual(TEXT("a valid root slot encodes"),
			TerrainPersistEncodeRootSlotBody(Root, Out), ETerrainPersistError::None);

		TArray<uint8> Dirty = Out;
		Dirty[1000] = 1;   // a byte in the reserved tail
		FTerrainRootSlot Back;
		TestEqual(TEXT("a nonzero byte in the reserved tail -> ReservedNotZero"),
			TerrainPersistDecodeRootSlotBody(Dirty, Back), ETerrainPersistError::ReservedNotZero);
	}

	// --- the encoder's own cap, enforced in every build configuration -----------------------
	{
		// P-004 section 10 caps a checkpoint descriptor body at 16,384 bytes. An over-cap body
		// must be refused by a RETURNED error, not by a check() that a shipping build removes:
		// an over-cap object written by a shipping server is one no decoder will ever accept.
		TArray<uint8> Oversized;
		Oversized.AddZeroed(TerrainPersistMaxCheckpointDescriptorBody + 1);

		TArray<uint8> Object;
		TestEqual(TEXT("an over-cap body -> CapExceeded"),
			TerrainPersistEncodeObject(ETerrainPersistObjectType::CheckpointDescriptor, Identity, Oversized, Object),
			ETerrainPersistError::CapExceeded);
		TestEqual(TEXT("and nothing is appended on that failure"), Object.Num(), 0);

		TArray<uint8> AtCap;
		AtCap.AddZeroed(TerrainPersistMaxCheckpointDescriptorBody);
		TestEqual(TEXT("a body exactly at the cap is accepted"),
			TerrainPersistEncodeObject(ETerrainPersistObjectType::CheckpointDescriptor, Identity, AtCap, Object),
			ETerrainPersistError::None);
	}

	// --- the Dense accessors refuse a wrong-sized buffer instead of reading past it ----------
	{
		TArray<uint8> Stunted;
		Stunted.AddZeroed(64);
		TestEqual(TEXT("a short Dense buffer reads zero rather than out of bounds"),
			(int32)TerrainPersistDenseDensityAt(Stunted, 0), 0);

		TArray<int16>  Densities; Densities.AddZeroed(TerrainChunkSampleCount);
		TArray<uint16> Materials; Materials.AddZeroed(TerrainChunkSampleCount);
		Densities[7] = 1234;
		TArray<uint8> Dense;
		TerrainPersistBuildDense(Densities, Materials, Dense);
		TestEqual(TEXT("a valid index still reads its sample"),
			(int32)TerrainPersistDenseDensityAt(Dense, 7), 1234);
		TestEqual(TEXT("an index past the chunk reads zero"),
			(int32)TerrainPersistDenseDensityAt(Dense, TerrainChunkSampleCount), 0);
		TestEqual(TEXT("a negative index reads zero"),
			(int32)TerrainPersistDenseDensityAt(Dense, -1), 0);
	}

	// --- the object-naming rule ------------------------------------------------------------
	{
		const FTerrainDigest Digest = MakeDigest(0x01);
		const FString Hex = TerrainPersistDigestToHex(Digest);
		TestEqual(TEXT("A digest renders as exactly 64 hex characters"), Hex.Len(), 64);

		FTerrainDigest Back;
		TestTrue(TEXT("It parses back"), TerrainPersistDigestFromHex(Hex, Back));
		TestTrue(TEXT("Round-trip is exact"), Back == Digest);

		TestFalse(TEXT("A short name is refused"), TerrainPersistDigestFromHex(Hex.Left(63), Back));
		TestFalse(TEXT("A long name is refused"), TerrainPersistDigestFromHex(Hex + TEXT("0"), Back));
		TestFalse(TEXT("Uppercase is refused"), TerrainPersistDigestFromHex(Hex.ToUpper(), Back));
		TestFalse(TEXT("A path separator is refused"),
			TerrainPersistDigestFromHex(TEXT("../../") + Hex.RightChop(6), Back));
		TestFalse(TEXT("Non-hex is refused"),
			TerrainPersistDigestFromHex(Hex.LeftChop(1) + TEXT("z"), Back));
	}

	return true;
}

// ==== TornTail ==========================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerrainPersistenceTornTailTest,
	"TerrainCore.Persistence.Format.TornTail",
	EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

bool FTerrainPersistenceTornTailTest::RunTest(const FString& Parameters)
{
	using namespace TerrainPersistTest;

	const FTerrainPersistIdentity Identity = MakeIdentity();
	const uint64 WorldTag = TerrainPersistWorldTag(Identity.World, Identity.Epoch);

	// A segment: header object, then five contiguous commit records.
	FTerrainJournalSegmentHeader SegmentHeader;
	SegmentHeader.SegmentId  = 1;
	SegmentHeader.FirstOpSeq = 1;

	TArray<uint8> HeaderBody;
	TerrainPersistEncodeSegmentHeaderBody(SegmentHeader, HeaderBody);

	TArray<uint8> Segment;
	TerrainPersistEncodeObject(ETerrainPersistObjectType::JournalSegmentHeader, Identity, HeaderBody, Segment);

	TArray<int32> RecordStarts;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		FTerrainJournalCommitRecord Record = MakeCommitRecord();
		Record.WorldTag  = WorldTag;
		Record.Op.OpSeq  = static_cast<FTerrainOpSeq>(Index + 1);
		Record.RequestId = static_cast<uint32>(Index + 1);

		RecordStarts.Add(Segment.Num());
		TestEqual(TEXT("Each record encodes"),
			TerrainPersistEncodeCommitRecord(Record, Segment), ETerrainPersistError::None);
	}

	// --- the whole, healthy segment -------------------------------------------------------
	{
		FTerrainJournalScanResult Result;
		TestEqual(TEXT("A healthy active segment scans"),
			TerrainPersistScanJournalSegment(Segment, Identity, true, Result), ETerrainPersistError::None);
		TestEqual(TEXT("Five commit records"), Result.CommitRecordCount, 5);
		TestEqual(TEXT("Last OpSeq is 5"), Result.LastOpSeq, (FTerrainOpSeq)5);
		TestFalse(TEXT("Not sealed"), Result.bSealed);
		TestFalse(TEXT("No torn tail"), Result.bTornTail);
		TestEqual(TEXT("Every byte is good"), Result.GoodBytes, Segment.Num());
	}

	// --- an incomplete FINAL record: the one legal torn tail --------------------------------
	{
		TArray<uint8> Torn = Segment;
		Torn.SetNum(Segment.Num() - 30);

		FTerrainJournalScanResult Result;
		TestEqual(TEXT("A torn final record scans on the ACTIVE segment"),
			TerrainPersistScanJournalSegment(Torn, Identity, true, Result), ETerrainPersistError::None);
		TestTrue (TEXT("It is reported as a torn tail"), Result.bTornTail);
		TestEqual(TEXT("Four complete records survive"), Result.CommitRecordCount, 4);
		TestEqual(TEXT("Last good OpSeq is 4"), Result.LastOpSeq, (FTerrainOpSeq)4);
		TestEqual(TEXT("GoodBytes points at the last complete record"), Result.GoodBytes, RecordStarts[4]);
		AddInfo(FString::Printf(TEXT("Torn tail of %d bytes preserved for diagnosis"), Result.TornTailBytes));

		// The same bytes on a segment that is NOT the active one are corruption.
		FTerrainJournalScanResult Inactive;
		const ETerrainPersistError InactiveError =
			TerrainPersistScanJournalSegment(Torn, Identity, false, Inactive);
		TestNotEqual(TEXT("The same tail on an inactive segment fails closed"),
			InactiveError, ETerrainPersistError::None);
		AddInfo(FString::Printf(TEXT("Inactive segment rejected with %s"),
			TerrainPersistErrorName(InactiveError)));
	}

	// --- a zero-filled tail: a torn append that extended the file without writing ------------
	{
		TArray<uint8> Zeroed = Segment;
		const int32 Cut = RecordStarts[4];
		for (int32 Index = Cut; Index < Zeroed.Num(); ++Index)
		{
			Zeroed[Index] = 0;
		}

		FTerrainJournalScanResult Result;
		TestEqual(TEXT("A zero-filled tail scans on the active segment"),
			TerrainPersistScanJournalSegment(Zeroed, Identity, true, Result), ETerrainPersistError::None);
		TestTrue (TEXT("and is reported as a torn tail"), Result.bTornTail);
		TestEqual(TEXT("with four complete records"), Result.CommitRecordCount, 4);

		// Garbage that is neither a frame nor zeros is not a tear.
		TArray<uint8> Garbage = Zeroed;
		Garbage[Garbage.Num() - 1] = 0x5A;
		FTerrainJournalScanResult GarbageResult;
		TestEqual(TEXT("A non-zero garbage tail fails closed"),
			TerrainPersistScanJournalSegment(Garbage, Identity, true, GarbageResult),
			ETerrainPersistError::BadMagic);
	}

	// --- damage to an INTERIOR record is never a torn tail -----------------------------------
	{
		TArray<uint8> Interior = Segment;
		Interior[RecordStarts[1] + 40] ^= 0x01;

		FTerrainJournalScanResult Result;
		const ETerrainPersistError Error =
			TerrainPersistScanJournalSegment(Interior, Identity, true, Result);
		TestEqual(TEXT("A damaged interior record fails closed"),
			Error, ETerrainPersistError::BodyChecksumMismatch);
	}

	// --- a sequence gap is corruption ---------------------------------------------------------
	{
		TArray<uint8> Gapped;
		TerrainPersistEncodeObject(ETerrainPersistObjectType::JournalSegmentHeader, Identity, HeaderBody, Gapped);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FTerrainJournalCommitRecord Record = MakeCommitRecord();
			Record.WorldTag  = WorldTag;
			Record.Op.OpSeq  = static_cast<FTerrainOpSeq>(Index == 2 ? 9 : Index + 1);
			Record.RequestId = static_cast<uint32>(Index + 1);
			TerrainPersistEncodeCommitRecord(Record, Gapped);
		}

		FTerrainJournalScanResult Result;
		TestEqual(TEXT("A sequence gap -> OrderViolation"),
			TerrainPersistScanJournalSegment(Gapped, Identity, true, Result),
			ETerrainPersistError::OrderViolation);
	}

	// --- sealing, and the rule that nothing follows a seal ------------------------------------
	{
		FTerrainJournalScanResult Open;
		TerrainPersistScanJournalSegment(Segment, Identity, true, Open);

		FTerrainJournalSealRecord Seal;
		Seal.WorldTag          = WorldTag;
		Seal.LastOpSeq         = Open.LastOpSeq;
		Seal.CommitRecordCount = static_cast<uint64>(Open.CommitRecordCount);
		Seal.RecordsDigest     = Open.RecordsDigest;
		Seal.SealedUtcMillis   = 1789412399999LL;

		TArray<uint8> Sealed = Segment;
		TestEqual(TEXT("The seal record encodes"),
			TerrainPersistEncodeSealRecord(Seal, Sealed), ETerrainPersistError::None);

		FTerrainJournalScanResult Result;
		TestEqual(TEXT("A sealed segment scans, on the inactive path"),
			TerrainPersistScanJournalSegment(Sealed, Identity, false, Result), ETerrainPersistError::None);
		TestTrue (TEXT("It is sealed"), Result.bSealed);
		TestEqual(TEXT("The seal agrees about the record count"),
			(int32)Result.Seal.CommitRecordCount, Result.CommitRecordCount);

		// A seal whose RecordsDigest does not match what the segment actually contains.
		FTerrainJournalSealRecord Lying = Seal;
		Lying.RecordsDigest = MakeDigest(0xEE);
		TArray<uint8> LyingSegment = Segment;
		TerrainPersistEncodeSealRecord(Lying, LyingSegment);

		FTerrainJournalScanResult LyingResult;
		TestEqual(TEXT("A seal that misreports its records digest fails closed"),
			TerrainPersistScanJournalSegment(LyingSegment, Identity, false, LyingResult),
			ETerrainPersistError::BodyChecksumMismatch);

		// Anything after a seal is a protocol violation.
		TArray<uint8> AfterSeal = Sealed;
		FTerrainJournalCommitRecord Extra = MakeCommitRecord();
		Extra.WorldTag = WorldTag;
		Extra.Op.OpSeq = 6;
		TerrainPersistEncodeCommitRecord(Extra, AfterSeal);

		FTerrainJournalScanResult AfterResult;
		TestEqual(TEXT("A record after the seal -> OrderViolation"),
			TerrainPersistScanJournalSegment(AfterSeal, Identity, false, AfterResult),
			ETerrainPersistError::OrderViolation);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

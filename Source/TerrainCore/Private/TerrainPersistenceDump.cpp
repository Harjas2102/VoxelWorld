// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#include "TerrainCore.h"
#include "TerrainPersistenceFormat.h"
#include "TerrainPersistenceRecords.h"
#include "TerrainPersistenceIndex.h"

#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"

/**
 * Terrain.PersistDump -- the inspect/dump command P-003 section 8 item 2 asks for.
 *
 * AGENTS.md section 4 requires persistence formats to be "versioned, flat, and INSPECTABLE".
 * A format nobody can read without a debugger is none of those things in practice, so this
 * exists from the first codec increment rather than after the first unexplained save.
 *
 * It is read-only and takes one argument: a path. It never writes, never repairs and never
 * truncates -- P-003's repair procedure is a later increment and deliberately is not this.
 *
 * A journal segment is recognised by its object type and scanned as a whole file; every other
 * object stands alone. Identity is reported, not enforced: the point of the command is to
 * diagnose a file that may well belong to a world you did not expect, so it does not refuse
 * to describe one.
 */

namespace
{
	FString DescribeDigest(const FTerrainDigest& Digest)
	{
		if (Digest.IsZero())
		{
			return TEXT("<zero>");
		}
		return TerrainPersistDigestToHex(Digest).Left(16) + TEXT("...");
	}

	FString DescribeIdBytes(const uint8* Bytes, int32 Count)
	{
		FString Out;
		Out.Reserve(Count * 2);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Out += FString::Printf(TEXT("%02x"), Bytes[Index]);
		}
		return Out;
	}

	const TCHAR* DescribeObjectType(ETerrainPersistObjectType Type)
	{
		switch (Type)
		{
		case ETerrainPersistObjectType::BaseDescriptor:       return TEXT("BaseDescriptor");
		case ETerrainPersistObjectType::ChunkPayload:         return TEXT("ChunkPayload");
		case ETerrainPersistObjectType::IndexPage:            return TEXT("IndexPage");
		case ETerrainPersistObjectType::CheckpointDescriptor: return TEXT("CheckpointDescriptor");
		case ETerrainPersistObjectType::RootSlot:             return TEXT("RootSlot");
		case ETerrainPersistObjectType::JournalSegmentHeader: return TEXT("JournalSegmentHeader");
		case ETerrainPersistObjectType::JournalAnchorSlot:    return TEXT("JournalAnchorSlot");
		}
		return TEXT("<unknown>");
	}

	const TCHAR* DescribeEncoding(ETerrainRegionEncoding Encoding)
	{
		switch (Encoding)
		{
		case ETerrainRegionEncoding::Dense:      return TEXT("Dense");
		case ETerrainRegionEncoding::SparseDiff: return TEXT("SparseDiff");
		case ETerrainRegionEncoding::Empty:      return TEXT("Empty");
		}
		return TEXT("<unknown>");
	}

	/**
	 * Reads the object type out of a header without validating the rest.
	 *
	 * The dump command has to name the type BEFORE it can pick a decoder, which is the one
	 * place where reading a field ahead of full validation is correct. Nothing is decided on
	 * this value except which decoder to call, and that decoder then validates everything.
	 */
	bool PeekObjectType(const TArray<uint8>& Bytes, ETerrainPersistObjectType& OutType)
	{
		if (Bytes.Num() < TerrainPersistObjectHeaderSize)
		{
			return false;
		}
		const uint8 TypeByte = Bytes[6];
		if (TypeByte == 0 || TypeByte > TerrainPersistObjectTypeMaxValue)
		{
			return false;
		}
		OutType = static_cast<ETerrainPersistObjectType>(TypeByte);
		return true;
	}

	void DumpChunkPayload(TArrayView<const uint8> Body, uint32 GeneratorVersion, uint8 ValueConfig)
	{
		FTerrainChunkPayloadRecord Record;
		const ETerrainPersistError Error =
			TerrainPersistDecodeChunkPayloadBody(Body, GeneratorVersion, ValueConfig, Record);
		if (Error != ETerrainPersistError::None)
		{
			UE_LOG(LogTerrainCore, Warning, TEXT("  body: %s"), TerrainPersistErrorName(Error));
			return;
		}

		UE_LOG(LogTerrainCore, Log, TEXT("  key=(%d,%d,%d) rev=%u lastOpSeq=%llu encoding=%s gen=%u valueConfig=%u"),
			Record.Key.X, Record.Key.Y, Record.Key.Z, Record.Rev, Record.LastOpSeq,
			DescribeEncoding(Record.Encoding), Record.GeneratorVersion, Record.ValueConfig);
		if (Record.Encoding == ETerrainRegionEncoding::SparseDiff)
		{
			UE_LOG(LogTerrainCore, Log, TEXT("  sparse samples=%d"), Record.Sparse.Num());
		}
	}

	void DumpSegment(const TArray<uint8>& Bytes, const FTerrainPersistIdentity& Identity)
	{
		// Both passes are reported. A file that scans clean as active and fails as inactive is
		// exactly the torn-tail case, and saying so is more useful than picking one answer.
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			const bool bActive = Pass == 0;
			FTerrainJournalScanResult Result;
			const ETerrainPersistError Error =
				TerrainPersistScanJournalSegment(Bytes, Identity, bActive, Result);

			if (Error != ETerrainPersistError::None)
			{
				UE_LOG(LogTerrainCore, Warning, TEXT("  scan(%s): %s"),
					bActive ? TEXT("active") : TEXT("inactive"), TerrainPersistErrorName(Error));
				continue;
			}

			UE_LOG(LogTerrainCore, Log,
				TEXT("  scan(%s): segment=%llu firstOpSeq=%llu commits=%d lastOpSeq=%llu sealed=%s tornTail=%s goodBytes=%d/%d"),
				bActive ? TEXT("active") : TEXT("inactive"),
				Result.Header.SegmentId, Result.Header.FirstOpSeq, Result.CommitRecordCount,
				Result.LastOpSeq, Result.bSealed ? TEXT("yes") : TEXT("no"),
				Result.bTornTail ? TEXT("yes") : TEXT("no"),
				Result.GoodBytes, Bytes.Num());

			if (Result.bTornTail)
			{
				UE_LOG(LogTerrainCore, Warning,
					TEXT("  %d trailing bytes are an incomplete final record. This command does not truncate."),
					Result.TornTailBytes);
			}
			if (Result.Header.bHasPredecessor)
			{
				UE_LOG(LogTerrainCore, Log, TEXT("  predecessor=%llu seal=%s"),
					Result.Header.PredecessorSegmentId,
					*DescribeDigest(Result.Header.PredecessorSealDigest));
			}
			return;   // the first pass that succeeds is the whole answer
		}
	}

	void DumpFile(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTerrainCore, Warning, TEXT("Terrain.PersistDump <file>"));
			return;
		}

		const FString Path = Args[0];
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path))
		{
			UE_LOG(LogTerrainCore, Warning, TEXT("Terrain.PersistDump: cannot read '%s'"), *Path);
			return;
		}

		UE_LOG(LogTerrainCore, Log, TEXT("Terrain.PersistDump '%s' (%d bytes)"), *Path, Bytes.Num());

		ETerrainPersistObjectType Type;
		if (!PeekObjectType(Bytes, Type))
		{
			UE_LOG(LogTerrainCore, Warning, TEXT("  not a schema-2 object: too short or unknown type byte"));
			return;
		}

		// Identity is read from the file itself and reported, never used to accept it. Decoding
		// with a null expectation is what lets this command describe a file from another world
		// instead of refusing to look at it.
		FTerrainPersistObjectHeader Header;
		TArrayView<const uint8> Body;
		int32 Consumed = 0;
		const ETerrainPersistError HeaderError =
			TerrainPersistDecodeObjectPrefix(Bytes, Type, nullptr, Header, Body, Consumed);
		if (HeaderError != ETerrainPersistError::None)
		{
			UE_LOG(LogTerrainCore, Warning, TEXT("  header: %s"), TerrainPersistErrorName(HeaderError));
			return;
		}

		UE_LOG(LogTerrainCore, Log, TEXT("  type=%s body=%llu bytes"),
			DescribeObjectType(Header.ObjectType), Header.BodyLength);
		UE_LOG(LogTerrainCore, Log, TEXT("  world=%s epoch=%s base=%s"),
			*DescribeIdBytes(Header.Identity.World.Bytes, TerrainPersistIdBytes),
			*DescribeIdBytes(Header.Identity.Epoch.Bytes, TerrainPersistIdBytes),
			*DescribeDigest(Header.Identity.BaseDigest));
		UE_LOG(LogTerrainCore, Log, TEXT("  contentDigest=%s"),
			*DescribeDigest(TerrainPersistDigest(Bytes)));

		switch (Header.ObjectType)
		{
		case ETerrainPersistObjectType::BaseDescriptor:
		{
			FTerrainBaseDescriptor Base;
			const ETerrainPersistError Error = TerrainPersistDecodeBaseDescriptorBody(Body, Base);
			if (Error != ETerrainPersistError::None)
			{
				UE_LOG(LogTerrainCore, Warning, TEXT("  body: %s"), TerrainPersistErrorName(Error));
				break;
			}
			UE_LOG(LogTerrainCore, Log, TEXT("  seed=%lld gen=%u(%s) kernel=%u(%s) catalog=%u/%u"),
				Base.Seed, Base.GeneratorVersion, *Base.GeneratorName,
				Base.BackendKernelVersion, *Base.BackendName,
				Base.MaterialCatalogVersion, Base.MaterialCatalogCount);
			UE_LOG(LogTerrainCore, Log, TEXT("  voxel=%lld um chunk=%d vox origin=(%lld,%lld,%lld) um valueConfig=%u"),
				Base.VoxelSizeMicrometres, Base.ChunkSizeVox,
				Base.OriginWorldMicrometres[0], Base.OriginWorldMicrometres[1], Base.OriginWorldMicrometres[2],
				Base.ValueConfig);
			UE_LOG(LogTerrainCore, Log, TEXT("  bounds=(%d,%d,%d)..(%d,%d,%d) paramsDigest=%s stamps=%d"),
				Base.WorldBoundsVox.Min.X, Base.WorldBoundsVox.Min.Y, Base.WorldBoundsVox.Min.Z,
				Base.WorldBoundsVox.Max.X, Base.WorldBoundsVox.Max.Y, Base.WorldBoundsVox.Max.Z,
				*DescribeDigest(Base.GeneratorParamsDigest), Base.AuthoredStamps.Num());
			break;
		}
		case ETerrainPersistObjectType::ChunkPayload:
		{
			// The expectations come from the file's own header here, because the dump command
			// has no base descriptor to hand. A real reader gets them from the loaded base.
			if (Body.Num() >= TerrainPersistChunkPayloadPrefix)
			{
				// Offsets 25..29 of the chunk payload body are GeneratorVersion and
				// ValueConfig (P-004 section 5). The decoder compares them against the base
				// descriptor; this command has none, so it reads them and compares them
				// against themselves rather than refusing to describe the file.
				FTerrainByteReader Reader(Body);
				Reader.Skip(25);
				const uint32 GeneratorVersion = Reader.ReadU32();
				const uint8  ValueConfig      = Reader.ReadU8();
				DumpChunkPayload(Body, GeneratorVersion, ValueConfig);
			}
			break;
		}
		case ETerrainPersistObjectType::IndexPage:
		{
			FTerrainIndexPage Page;
			const ETerrainPersistError Error = TerrainIndexDecodePageBody(Body, Page);
			if (Error != ETerrainPersistError::None)
			{
				UE_LOG(LogTerrainCore, Warning, TEXT("  body: %s"), TerrainPersistErrorName(Error));
				break;
			}
			UE_LOG(LogTerrainCore, Log, TEXT("  depth=%u kind=%s entries=%d prefix=%s"),
				Page.Depth, Page.bLeaf ? TEXT("Leaf") : TEXT("Internal"),
				Page.bLeaf ? Page.Leaves.Num() : Page.Internal.Num(),
				*DescribeIdBytes(Page.KeyPrefix, TerrainPersistIndexKeyBytes));
			break;
		}
		case ETerrainPersistObjectType::CheckpointDescriptor:
		{
			FTerrainCheckpointDescriptor Descriptor;
			const ETerrainPersistError Error = TerrainPersistDecodeCheckpointBody(Body, Descriptor);
			if (Error != ETerrainPersistError::None)
			{
				UE_LOG(LogTerrainCore, Warning, TEXT("  body: %s"), TerrainPersistErrorName(Error));
				break;
			}
			UE_LOG(LogTerrainCore, Log, TEXT("  G=%llu generation=%llu root=%s len=%u keys=%llu payloadBytes=%llu"),
				Descriptor.G, Descriptor.Generation,
				Descriptor.bHasRootPage ? *DescribeDigest(Descriptor.RootPageDigest) : TEXT("<none>"),
				Descriptor.RootPageLength, Descriptor.LeafKeyCount, Descriptor.TotalPayloadBytes);
			break;
		}
		case ETerrainPersistObjectType::RootSlot:
		{
			FTerrainRootSlot Slot;
			const ETerrainPersistError Error = TerrainPersistDecodeRootSlotBody(Body, Slot);
			if (Error != ETerrainPersistError::None)
			{
				UE_LOG(LogTerrainCore, Warning, TEXT("  body: %s"), TerrainPersistErrorName(Error));
				break;
			}
			UE_LOG(LogTerrainCore, Log, TEXT("  generation=%llu G=%llu descriptor=%s len=%u storeFormat=%u"),
				Slot.Generation, Slot.G, *DescribeDigest(Slot.DescriptorDigest),
				Slot.DescriptorLength, Slot.StoreFormatVersion);
			break;
		}
		case ETerrainPersistObjectType::JournalAnchorSlot:
		{
			FTerrainJournalAnchor Anchor;
			const ETerrainPersistError Error = TerrainPersistDecodeAnchorBody(Body, Anchor);
			if (Error != ETerrainPersistError::None)
			{
				UE_LOG(LogTerrainCore, Warning, TEXT("  body: %s"), TerrainPersistErrorName(Error));
				break;
			}
			UE_LOG(LogTerrainCore, Log, TEXT("  anchorGen=%llu active=%llu firstOpSeq=%llu predecessor=%llu lastOpSeq=%llu"),
				Anchor.AnchorGeneration, Anchor.ActiveSegmentId, Anchor.ActiveSegmentFirstOpSeq,
				Anchor.PredecessorSegmentId, Anchor.PredecessorLastOpSeq);
			break;
		}
		case ETerrainPersistObjectType::JournalSegmentHeader:
			DumpSegment(Bytes, Header.Identity);
			break;
		}

		if (Header.ObjectType != ETerrainPersistObjectType::JournalSegmentHeader && Consumed != Bytes.Num())
		{
			UE_LOG(LogTerrainCore, Warning, TEXT("  %d trailing bytes after the object"),
				Bytes.Num() - Consumed);
		}
	}
}

static FAutoConsoleCommand GTerrainPersistDumpCommand(
	TEXT("Terrain.PersistDump"),
	TEXT("Terrain.PersistDump <file> - describes a schema-2 persistence object or journal segment. Read-only."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&DumpFile));

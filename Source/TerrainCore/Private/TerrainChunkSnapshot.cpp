// Copyright VoxelWorld. See Docs/proposals/P-008-join-in-progress.md.

#include "TerrainChunkSnapshot.h"

#include "TerrainChunk.h"
#include "Misc/Compression.h"

namespace
{
	constexpr int32 DenseBytes = TerrainChunkSampleCount * 4;   // §4.2 transfer layout
}

bool TerrainEncodeChunkSnapshot(TArrayView<const uint8> Dense, TArray<uint8>& OutCompressed)
{
	OutCompressed.Reset();
	if (Dense.Num() != DenseBytes)
	{
		return false;
	}

	int32 Bound = FCompression::CompressMemoryBound(NAME_Zlib, DenseBytes);
	OutCompressed.SetNumUninitialized(Bound);
	if (!FCompression::CompressMemory(NAME_Zlib, OutCompressed.GetData(), Bound, Dense.GetData(), DenseBytes)
		|| Bound <= 0 || Bound > TerrainSnapshotMaxCompressedBytes)
	{
		OutCompressed.Reset();
		return false;
	}
	OutCompressed.SetNum(Bound, EAllowShrinking::No);
	return true;
}

bool TerrainDecodeChunkSnapshot(TArrayView<const uint8> Compressed, TArray<uint8>& OutDense)
{
	OutDense.Reset();
	if (Compressed.Num() <= 0 || Compressed.Num() > TerrainSnapshotMaxCompressedBytes)
	{
		return false;
	}
	OutDense.SetNumUninitialized(DenseBytes);
	// UncompressMemory with an exact output size fails on a stream that decodes to any other
	// length, so a truncated or padded snapshot can never become a partly written chunk.
	if (!FCompression::UncompressMemory(NAME_Zlib, OutDense.GetData(), DenseBytes,
	                                    Compressed.GetData(), Compressed.Num()))
	{
		OutDense.Reset();
		return false;
	}
	return true;
}

bool TerrainSplitChunkSnapshot(TArrayView<const uint8> Compressed, TArray<TArray<uint8>>& OutFragments)
{
	OutFragments.Reset();
	if (Compressed.Num() <= 0 || Compressed.Num() > TerrainSnapshotMaxCompressedBytes)
	{
		return false;
	}
	for (int32 Offset = 0; Offset < Compressed.Num(); Offset += TerrainSnapshotFragmentBytes)
	{
		const int32 Length = FMath::Min(TerrainSnapshotFragmentBytes, Compressed.Num() - Offset);
		OutFragments.Emplace(Compressed.GetData() + Offset, Length);
	}
	return OutFragments.Num() <= TerrainSnapshotMaxFragments;
}

void FTerrainSnapshotAssembler::Reset()
{
	bActive    = false;
	Generation = 0;
	Rev        = 0;
	Count      = 0;
	TotalBytes = 0;
	NextIndex  = 0;
	Buffer.Reset();
}

FTerrainSnapshotAssembler::EResult FTerrainSnapshotAssembler::Add(
	const FTerrainSnapshotFragmentView& Fragment, TArray<uint8>& OutCompressed, FTerrainChunkKey& OutDiscarded)
{
	OutCompressed.Reset();
	OutDiscarded = Fragment.Key;

	const auto Reject = [&]()
	{
		if (bActive)
		{
			OutDiscarded = Key;   // the chunk whose partial snapshot is lost
		}
		Reset();
		return EResult::Rejected;
	};

	const bool bHeaderSane = Fragment.Count >= 1 && Fragment.Count <= TerrainSnapshotMaxFragments
		&& Fragment.Index >= 0 && Fragment.Index < Fragment.Count
		&& Fragment.TotalBytes > 0 && Fragment.TotalBytes <= TerrainSnapshotMaxCompressedBytes
		&& Fragment.Rev != 0
		&& Fragment.Bytes.Num() > 0 && Fragment.Bytes.Num() <= TerrainSnapshotFragmentBytes;
	if (!bHeaderSane)
	{
		return Reject();
	}

	if (!bActive)
	{
		if (Fragment.Index != 0)
		{
			return Reject();
		}
		bActive    = true;
		Key        = Fragment.Key;
		Generation = Fragment.Generation;
		Rev        = Fragment.Rev;
		Count      = Fragment.Count;
		TotalBytes = Fragment.TotalBytes;
		NextIndex  = 0;
		Buffer.Reset(TotalBytes);
	}
	else if (!(Fragment.Key == Key) || Fragment.Generation != Generation || Fragment.Rev != Rev
		|| Fragment.Count != Count || Fragment.TotalBytes != TotalBytes || Fragment.Index != NextIndex)
	{
		return Reject();
	}

	// Every fragment but the last is exactly full; the last carries the remainder exactly.
	const bool bLast = Fragment.Index == Count - 1;
	const int32 Expected = bLast ? TotalBytes - Buffer.Num() : TerrainSnapshotFragmentBytes;
	if (Fragment.Bytes.Num() != Expected)
	{
		return Reject();
	}

	Buffer.Append(Fragment.Bytes.GetData(), Fragment.Bytes.Num());
	++NextIndex;
	if (!bLast)
	{
		return EResult::Incomplete;
	}

	OutCompressed = MoveTemp(Buffer);
	Reset();
	return EResult::Complete;
}

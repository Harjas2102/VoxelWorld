// Copyright VoxelWorld. See Docs/proposals/P-008-join-in-progress.md.

#pragma once

#include "CoreMinimal.h"
#include "TerrainTypes.h"

/**
 * TerrainChunkSnapshot.h -- one modified chunk, on the wire (P-008, build step 5).
 *
 * A snapshot is the §4.2 Dense transfer layout (131,072 bytes: int16 densities then uint16
 * materials), Zlib-compressed, cut into fragments small enough to stay far below UE's partial-
 * bunch reassembly limit. Nothing here knows about sockets, actors or RPCs, so all of it tests
 * headless: the stream component only carries the fragments.
 *
 * WHY NOT THE P-004 CHUNK PAYLOAD. That format carries a world identity, a digest and a
 * SparseDiff encoding against a generator baseline, and every one of those answers a
 * question the disk has and the wire does not: a snapshot is consumed once, immediately, by a
 * client whose world identity the session already checked. Compression does the job SparseDiff
 * would -- a dug chunk is overwhelmingly uniform rock and air -- without a baseline decoder
 * that does not exist yet (P-004 §5.3 restores Dense only).
 */

/** Largest fragment body. Well under net.MaxConstructedPartialBunchSizeBytes (64 KiB). */
inline constexpr int32 TerrainSnapshotFragmentBytes = 16 * 1024;

/** Most fragments one snapshot may use. Zlib's worst case for 128 KiB is ~128 KiB + 5% header. */
inline constexpr int32 TerrainSnapshotMaxFragments = 10;

/** Largest compressed snapshot accepted, which bounds what an assembler will buffer. */
inline constexpr int32 TerrainSnapshotMaxCompressedBytes =
	TerrainSnapshotFragmentBytes * TerrainSnapshotMaxFragments;

/** Compresses a Dense region payload. False unless the input is exactly the Dense size. */
TERRAINCORE_API bool TerrainEncodeChunkSnapshot(TArrayView<const uint8> Dense, TArray<uint8>& OutCompressed);

/** Decompresses to exactly the Dense size, or fails. Never returns a partial chunk. */
TERRAINCORE_API bool TerrainDecodeChunkSnapshot(TArrayView<const uint8> Compressed, TArray<uint8>& OutDense);

/** Splits a compressed snapshot into fragment bodies of at most TerrainSnapshotFragmentBytes. */
TERRAINCORE_API bool TerrainSplitChunkSnapshot(TArrayView<const uint8> Compressed, TArray<TArray<uint8>>& OutFragments);

/** One fragment as the receiver sees it. Mirrors the replicated struct field for field. */
struct FTerrainSnapshotFragmentView
{
	FTerrainChunkKey        Key;
	uint32                  Generation = 0;
	FTerrainRev             Rev = 0;
	int32                   Index = 0;
	int32                   Count = 0;
	int32                   TotalBytes = 0;
	TArrayView<const uint8> Bytes;
};

/**
 * Reassembles one client's snapshot fragments.
 *
 * The transport is one reliable, ordered channel and a sender emits every fragment of a
 * snapshot consecutively, so a well-behaved stream never interleaves two snapshots. The
 * assembler is still strict, because being strict is cheaper than being right about the
 * sender forever: a fragment that does not continue the snapshot in progress -- another key,
 * another generation, a skipped index, a different header -- discards the partial one, and
 * the caller reports that chunk as unsynced rather than guessing.
 */
class TERRAINCORE_API FTerrainSnapshotAssembler
{
public:
	enum class EResult : uint8
	{
		Incomplete,   // accepted; more fragments to come
		Complete,     // OutCompressed holds the whole snapshot
		Rejected,     // malformed or out of sequence; any partial snapshot was discarded
	};

	/**
	 * Feeds one fragment. On Rejected, OutDiscarded names the key whose partial snapshot was
	 * dropped (or the fragment's own key), so the caller can ask for that chunk again.
	 */
	EResult Add(const FTerrainSnapshotFragmentView& Fragment, TArray<uint8>& OutCompressed, FTerrainChunkKey& OutDiscarded);

	bool IsAssembling() const { return bActive; }

private:
	void Reset();

	bool             bActive = false;
	FTerrainChunkKey Key;
	uint32           Generation = 0;
	FTerrainRev      Rev = 0;
	int32            Count = 0;
	int32            TotalBytes = 0;
	int32            NextIndex = 0;
	TArray<uint8>    Buffer;
};

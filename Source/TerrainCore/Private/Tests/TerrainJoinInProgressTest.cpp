// Copyright VoxelWorld. See Docs/proposals/P-008-join-in-progress.md.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "TerrainReplica.h"
#include "TerrainChunkSnapshot.h"
#include "TerrainRevisionIndex.h"
#include "TerrainOpGeometry.h"
#include "TerrainChunk.h"
#include "MemoryTerrainBackend.h"
#include "ITerrainDensityField.h"

/**
 * TerrainCore.Replication.JoinInProgress -- P-008, build step 5, DEF-3.
 *
 * The protocol's claim is that a replica which (a) installs each snapshot at the point in the
 * ordered stream where the server took it, and (b) checks and bumps revisions only for chunks
 * it holds in sync, ends up EQUAL to the server -- even though its snapshots were taken at
 * different cuts while multi-chunk edits kept arriving, and even after an op is lost.
 *
 * Two negative controls keep the test honest. A replica that skips one snapshot must diverge,
 * or the snapshots are not what made it converge. And replaying an op into a chunk whose
 * snapshot already contains it must diverge, or DEF-3's hazard is not being exercised.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainJoinInProgressTest, "TerrainCore.Replication.JoinInProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext
	| EAutomationTestFlags::ServerContext | EAutomationTestFlags::CommandletContext
	| EAutomationTestFlags::ProductFilter)

namespace TerrainJoinTest
{
	class FJoinField final : public ITerrainDensityField
	{
	public:
		virtual FTerrainDensitySample Sample(FIntVector Position) const override
		{
			FTerrainDensitySample Out;
			Out.Density    = Position.Z < 0 ? -1.f : 1.f;
			Out.MaterialId = Position.Z < -8 ? 5 : (Position.Z < 0 ? 4 : 1);
			return Out;
		}
	};

	FTerrainOp MakeOp(ETerrainOpKind Kind, const FIntVector& Centre, int32 RadiusVox, FTerrainOpSeq OpSeq)
	{
		FTerrainOp Op;
		Op.Kind         = Kind;
		Op.Shape        = ETerrainShape::Sphere;
		Op.Source       = ETerrainSource::Player;
		Op.SourceId     = 2;
		Op.CentreVox    = Centre;
		Op.RadiusVoxQ16 = RadiusVox << 16;
		Op.OpSeq        = OpSeq;
		Op.MaterialId   = Kind == ETerrainOpKind::Add ? 7 : 0;
		return Op;
	}

	/** The authority: its backend, its revisions, and the commit that produces a wire revision list. */
	struct FServer
	{
		FMemoryTerrainBackend Backend;
		FTerrainRevisionIndex Revisions;

		bool Commit(const FTerrainOp& Op, TArray<FTerrainChunkRevision>& Out)
		{
			Out.Reset();
			FTerrainBox Bounds; TArray<FTerrainChunkKey> Keys;
			if (!TerrainOpBounds(Op, Bounds) || !TerrainChunkKeysForBox(Bounds, Keys)) return false;
			for (const FTerrainChunkKey& K : Keys)
			{
				FTerrainChunkRevision V; V.Key = FIntVector(K.X, K.Y, K.Z); V.Before = Revisions.GetRevision(K);
				Out.Add(V);
			}
			FTerrainEditResult Result;
			if (!Backend.ApplyOp(Op, Result) || !Revisions.TryBumpRevisions(Result.AffectedChunks)) return false;
			for (FTerrainChunkRevision& V : Out) V.After = Revisions.GetRevision(FTerrainChunkKey(V.Key.X, V.Key.Y, V.Key.Z));
			return true;
		}

		bool Snapshot(const FTerrainChunkKey& Key, TArray<uint8>& OutCompressed, FTerrainRev& OutRev)
		{
			FTerrainRegionData Region;
			OutRev = Revisions.GetRevision(Key);
			return Backend.ReadRegion(Key, Region) && Region.Encoding == ETerrainRegionEncoding::Dense
				&& TerrainEncodeChunkSnapshot(Region.Payload, OutCompressed);
		}
	};

	/** A client: its own backend, revisions and replica state. */
	struct FClient
	{
		FMemoryTerrainBackend Backend;
		FTerrainRevisionIndex Revisions;
		FTerrainReplica       Replica;
	};

	TArray<uint8> Noise(int32 Length, uint32 Seed)
	{
		TArray<uint8> Out; Out.SetNumUninitialized(Length);
		uint32 X = Seed;
		for (int32 I = 0; I < Length; ++I) { X = X * 1664525u + 1013904223u; Out[I] = uint8(X >> 24); }
		return Out;
	}
}

bool FTerrainJoinInProgressTest::RunTest(const FString& Parameters)
{
	using namespace TerrainJoinTest;
	using EResult = FTerrainSnapshotAssembler::EResult;

	// The truncated-snapshot check below makes the engine log this; the refusal is the point.
	AddExpectedError(TEXT("Failed to uncompress memory"), EAutomationExpectedErrorFlags::Contains, 1);

	TSharedPtr<FJoinField, ESPMode::ThreadSafe> Field = MakeShared<FJoinField, ESPMode::ThreadSafe>();
	FTerrainBackendInit Init;
	Init.GeneratorVersion  = 7;
	Init.VoxelSizeCm       = 50.f;
	Init.WorldBoundsVox    = FTerrainBox(FIntVector(-256,-256,-256), FIntVector(256,256,256));
	Init.DensityField      = Field.Get();
	Init.DensityFieldOwner = Field;

	FTerrainStreamingInterest Interest;
	Interest.InterestId = 1; Interest.WorldLocation = FVector::ZeroVector; Interest.RadiusCm = 3000.0; Interest.bCollision = true;

	const auto Start = [&](ITerrainBackend& Backend, ETerrainRole Role)
	{
		FTerrainBackendInit Mine = Init; Mine.Role = Role;
		const bool bOk = Backend.Initialize(Mine);
		Backend.SetStreamingInterest(Interest);
		return bOk;
	};

	// ===== the codec ======================================================================
	{
		FServer Server;
		if (!Start(Server.Backend, ETerrainRole::Server)) { AddError(TEXT("Server init failed")); return false; }
		TArray<FTerrainChunkRevision> Revs;
		TestTrue(TEXT("Dig for the codec"), Server.Commit(MakeOp(ETerrainOpKind::Remove, FIntVector(8, 8, -4), 5, 1), Revs));

		const FTerrainChunkKey Key(0, 0, -1);
		FTerrainRegionData Region;
		TestTrue(TEXT("Read a dug chunk"), Server.Backend.ReadRegion(Key, Region));
		TArray<uint8> Compressed, Back;
		TestTrue (TEXT("A dug chunk encodes"), TerrainEncodeChunkSnapshot(Region.Payload, Compressed));
		TestTrue (TEXT("and compresses far below Dense"), Compressed.Num() < Region.Payload.Num() / 8);
		TestTrue (TEXT("and decodes"), TerrainDecodeChunkSnapshot(Compressed, Back));
		TestTrue (TEXT("to the same bytes"), Back == Region.Payload);
		AddInfo(FString::Printf(TEXT("Dug chunk snapshot: %d bytes (Dense %d)"), Compressed.Num(), Region.Payload.Num()));

		TArray<uint8> Short(Compressed.GetData(), Compressed.Num() - 1);
		TestFalse(TEXT("A truncated snapshot never decodes"), TerrainDecodeChunkSnapshot(Short, Back));
		TestEqual(TEXT("and leaves nothing behind"), Back.Num(), 0);
		TestFalse(TEXT("A wrong-size input never encodes"), TerrainEncodeChunkSnapshot(TArray<uint8>(Region.Payload.GetData(), 100), Compressed));

		// Incompressible content: the worst case, many fragments.
		const TArray<uint8> Hard = Noise(TerrainChunkSampleCount * 4, 12345);
		TArray<uint8> HardCompressed;
		TestTrue(TEXT("Noise still encodes within the cap"), TerrainEncodeChunkSnapshot(Hard, HardCompressed));
		TArray<TArray<uint8>> Pieces;
		TestTrue(TEXT("and splits"), TerrainSplitChunkSnapshot(HardCompressed, Pieces));
		TestTrue(TEXT("into several fragments"), Pieces.Num() >= 8 && Pieces.Num() <= TerrainSnapshotMaxFragments);
		AddInfo(FString::Printf(TEXT("Worst-case snapshot: %d bytes in %d fragments"), HardCompressed.Num(), Pieces.Num()));

		const auto Feed = [&](FTerrainSnapshotAssembler& A, int32 Index, uint32 Generation, TArray<uint8>& Out, FTerrainChunkKey& Discarded,
		                      int32 Length = -1)
		{
			FTerrainSnapshotFragmentView F;
			F.Key = Key; F.Generation = Generation; F.Rev = 3; F.Index = Index; F.Count = Pieces.Num();
			F.TotalBytes = HardCompressed.Num();
			F.Bytes = Length < 0 ? TArrayView<const uint8>(Pieces[Index]) : TArrayView<const uint8>(Pieces[Index].GetData(), Length);
			return A.Add(F, Out, Discarded);
		};

		{
			FTerrainSnapshotAssembler A; TArray<uint8> Out; FTerrainChunkKey Discarded;
			bool bInOrder = true;
			for (int32 I = 0; I + 1 < Pieces.Num(); ++I) bInOrder &= Feed(A, I, 1, Out, Discarded) == EResult::Incomplete;
			TestTrue (TEXT("Fragments in order are accepted"), bInOrder);
			TestEqual(TEXT("and the last completes the snapshot"), Feed(A, Pieces.Num() - 1, 1, Out, Discarded), EResult::Complete);
			TestTrue (TEXT("byte for byte"), Out == HardCompressed);
			TArray<uint8> Dense;
			TestTrue (TEXT("and it decodes to the original"), TerrainDecodeChunkSnapshot(Out, Dense) && Dense == Hard);
		}
		{
			FTerrainSnapshotAssembler A; TArray<uint8> Out; FTerrainChunkKey Discarded;
			TestEqual(TEXT("A snapshot cannot start mid-way"), Feed(A, 1, 1, Out, Discarded), EResult::Rejected);
			Feed(A, 0, 1, Out, Discarded);
			TestEqual(TEXT("A skipped fragment is rejected"), Feed(A, 2, 1, Out, Discarded), EResult::Rejected);
			TestTrue (TEXT("naming the chunk that was lost"), Discarded == Key);
			TestFalse(TEXT("and the partial snapshot is gone"), A.IsAssembling());
			Feed(A, 0, 1, Out, Discarded);
			TestEqual(TEXT("Another generation mid-snapshot is rejected"), Feed(A, 1, 2, Out, Discarded), EResult::Rejected);
			Feed(A, 0, 1, Out, Discarded);
			TestEqual(TEXT("A short middle fragment is rejected"), Feed(A, 1, 1, Out, Discarded, 100), EResult::Rejected);
		}
	}

	// ===== the protocol: a join while edits continue =========================================
	FServer Server;
	FClient Client;          // follows P-008 exactly
	FClient Skipper;         // negative control: never receives one snapshot
	if (!Start(Server.Backend, ETerrainRole::Server) || !Start(Client.Backend, ETerrainRole::Client)
		|| !Start(Skipper.Backend, ETerrainRole::Client))
	{
		AddError(TEXT("Backend init failed"));
		return false;
	}

	// Chunks either side of x=0 and y=0, at z=-1: every op below straddles several of them.
	const FTerrainChunkKey A(0, 0, -1), B(-1, 0, -1), C(0, -1, -1), D(-1, -1, -1);
	const FTerrainChunkKey Watched[] = { A, B, C, D, FTerrainChunkKey(0,0,0), FTerrainChunkKey(-1,0,0),
	                                     FTerrainChunkKey(0,-1,0), FTerrainChunkKey(-1,-1,0) };

	FTerrainOpSeq Seq = 1;
	TArray<FTerrainChunkRevision> Revs;
	// History before the client exists: this is what a restart or a late join inherits.
	const FTerrainOp History[] = {
		MakeOp(ETerrainOpKind::Remove, FIntVector( 0,  0, -4), 5, 0),
		MakeOp(ETerrainOpKind::Add,    FIntVector( 2,  1, -3), 3, 0),
		MakeOp(ETerrainOpKind::Remove, FIntVector(-3,  2, -5), 4, 0),
		MakeOp(ETerrainOpKind::Remove, FIntVector( 1, -3, -6), 4, 0),
	};
	for (FTerrainOp Op : History) { Op.OpSeq = Seq++; TestTrue(TEXT("History commits"), Server.Commit(Op, Revs)); }

	// The client joins. Pristine chunks are announced; the edited ones need snapshots.
	TArray<FTerrainChunkKey> Pristine, Edited;
	for (const FTerrainChunkKey& K : Watched) (Server.Revisions.GetRevision(K) == 0 ? Pristine : Edited).Add(K);
	TestTrue(TEXT("The history edited all four z=-1 chunks"), Edited.Contains(A) && Edited.Contains(B) && Edited.Contains(C) && Edited.Contains(D));
	TestTrue(TEXT("The client accepts the pristine chunks"), Client.Replica.AcceptPristine(Client.Revisions, Pristine));
	Skipper.Replica.AcceptPristine(Skipper.Revisions, Pristine);

	const auto Deliver = [&](FClient& To, const FTerrainOp& Op, const TArray<FTerrainChunkRevision>& R, TArray<FTerrainChunkKey>& Lost)
	{ return To.Replica.ApplyOp(To.Backend, To.Revisions, Op, R, Lost); };
	const auto SendSnapshot = [&](FClient& To, const FTerrainChunkKey& K)
	{
		TArray<uint8> Compressed; FTerrainRev Rev = 0;
		return Server.Snapshot(K, Compressed, Rev) && To.Replica.ApplySnapshot(To.Backend, To.Revisions, K, Rev, Compressed);
	};
	const auto Live = [&](ETerrainOpKind Kind, const FIntVector& Centre, int32 Radius, bool bToClient, TArray<FTerrainChunkKey>& Lost)
	{
		Lost.Reset();
		const FTerrainOp Op = MakeOp(Kind, Centre, Radius, Seq++);
		if (!Server.Commit(Op, Revs)) return false;
		TArray<FTerrainChunkKey> Ignored;
		Deliver(Skipper, Op, Revs, Ignored);
		return !bToClient || Deliver(Client, Op, Revs, Lost);
	};

	TArray<FTerrainChunkKey> Lost;
	// Snapshots one at a time, with multi-chunk edits between them -- DEF-3's exact shape.
	TestTrue(TEXT("Snapshot A at cut 4"), SendSnapshot(Client, A) && SendSnapshot(Skipper, A));
	TestTrue(TEXT("Live op 5 (A, B, C, D)"), Live(ETerrainOpKind::Add, FIntVector(0, 0, -2), 4, true, Lost));
	TestEqual(TEXT("Nothing synced was lost"), Lost.Num(), 0);
	TestFalse(TEXT("B is still unsynced, though op 5 wrote into it"), Client.Replica.IsSynced(B));
	TestEqual(TEXT("and its revision was not touched"), Client.Revisions.GetRevision(B), (FTerrainRev)0);
	TestEqual(TEXT("A followed the server"), Client.Revisions.GetRevision(A), Server.Revisions.GetRevision(A));

	TestTrue(TEXT("Snapshot B at cut 5 -- it already contains op 5"), SendSnapshot(Client, B));
	// The Skipper never gets B's snapshot.
	TestTrue(TEXT("Live op 6"), Live(ETerrainOpKind::Remove, FIntVector(-1, 0, -3), 4, true, Lost));
	TestEqual(TEXT("Op 6 lost nothing"), Lost.Num(), 0);
	TestTrue(TEXT("Snapshots C and D at cut 6"), SendSnapshot(Client, C) && SendSnapshot(Client, D)
		&& SendSnapshot(Skipper, C) && SendSnapshot(Skipper, D));
	for (const FTerrainChunkKey& K : Edited)
	{
		if (K == A || K == B || K == C || K == D) continue;
		TestTrue(TEXT("Snapshot of another edited chunk at cut 6"), SendSnapshot(Client, K) && SendSnapshot(Skipper, K));
	}

	// An op is lost on the way to the client.
	TestTrue(TEXT("Live op 7, dropped"), Live(ETerrainOpKind::Add, FIntVector(0, -1, -2), 3, false, Lost));
	TestTrue(TEXT("Live op 8"), Live(ETerrainOpKind::Remove, FIntVector(0, 0, -3), 4, true, Lost));
	TestTrue(TEXT("The gap is detected in the chunks op 7 changed"), Lost.Num() > 0);
	for (const FTerrainChunkKey& K : Lost) TestFalse(TEXT("A lost chunk is unsynced"), Client.Replica.IsSynced(K));
	const TArray<FTerrainChunkKey> Repair = Lost;
	TestTrue(TEXT("Live op 9, while the repair is pending"), Live(ETerrainOpKind::Add, FIntVector(-2, -2, -2), 3, true, Lost));
	for (const FTerrainChunkKey& K : Repair) TestTrue(TEXT("Resync snapshot"), SendSnapshot(Client, K));
	TestTrue(TEXT("Live op 10"), Live(ETerrainOpKind::Remove, FIntVector(1, 1, -4), 3, true, Lost));
	TestEqual(TEXT("After the repair nothing is lost"), Lost.Num(), 0);

	bool bAllEqual = true, bAllSynced = true;
	for (const FTerrainChunkKey& K : Watched)
	{
		const uint64 Want = Server.Backend.HashRegion(K);
		if (Client.Backend.HashRegion(K) != Want)
		{
			bAllEqual = false;
			AddError(FString::Printf(TEXT("Chunk (%d,%d,%d) diverged"), K.X, K.Y, K.Z));
		}
		if (Client.Revisions.GetRevision(K) != Server.Revisions.GetRevision(K)) bAllEqual = false;
		bAllSynced &= Client.Replica.IsSynced(K);
	}
	TestTrue(TEXT("Every watched chunk equals the server, data and revision"), bAllEqual);
	TestTrue(TEXT("and every one is synced"), bAllSynced);

	// ===== negative controls ===============================================================
	TestNotEqual(TEXT("Control: without B's snapshot, B diverges"),
		Skipper.Backend.HashRegion(B), Server.Backend.HashRegion(B));
	TestFalse(TEXT("and the replica knows B is not synced"), Skipper.Replica.IsSynced(B));

	{
		// DEF-3's hazard, made real: a chunk whose snapshot already contains op N, then op N again,
		// after a later op that does not commute with it. Ordering is the only thing preventing it.
		FServer Solo;
		Start(Solo.Backend, ETerrainRole::Server);
		const FTerrainOp First  = MakeOp(ETerrainOpKind::Remove, FIntVector(4, 4, -4), 4, 1);
		const FTerrainOp Second = MakeOp(ETerrainOpKind::Add,    FIntVector(5, 4, -4), 3, 2);
		Solo.Commit(First, Revs); Solo.Commit(Second, Revs);
		FClient Late; Start(Late.Backend, ETerrainRole::Client);
		TArray<uint8> Compressed; FTerrainRev Rev = 0;
		TestTrue(TEXT("Control: snapshot after both ops"), Solo.Snapshot(A, Compressed, Rev)
			&& Late.Replica.ApplySnapshot(Late.Backend, Late.Revisions, A, Rev, Compressed));
		TestEqual(TEXT("which equals the server"), Late.Backend.HashRegion(A), Solo.Backend.HashRegion(A));
		FTerrainEditResult Ignored;
		Late.Backend.ApplyOp(First, Ignored);   // replayed into a chunk that already contains it
		TestNotEqual(TEXT("Control: replaying an op the snapshot already holds diverges"),
			Late.Backend.HashRegion(A), Solo.Backend.HashRegion(A));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

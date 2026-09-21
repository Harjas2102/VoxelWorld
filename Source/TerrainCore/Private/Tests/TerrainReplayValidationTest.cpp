// Copyright VoxelWorld. Client replay must reject a broken revision chain before mutation.
#if WITH_DEV_AUTOMATION_TESTS
#include "TerrainService.h"
#include "TerrainChunk.h"
#include "MemoryTerrainBackend.h"
#include "TerrainChunkSnapshot.h"
#include "TerrainWorldField.h"
#include "Misc/AutomationTest.h"
#include "Subsystems/SubsystemCollection.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainReplayValidationTest,"TerrainCore.Replay.Validation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTerrainReplayValidationTest::RunTest(const FString&)
{
    TStrongObjectPtr<UTerrainService> S(NewObject<UTerrainService>());
    FSubsystemCollection<UWorldSubsystem> Collection; S->Initialize(Collection);
    S->ActiveInit.WorldBoundsVox=FTerrainBox(FIntVector(-64),FIntVector(64));
    S->DensityField=MakeShared<FTerrainWorldField,ESPMode::ThreadSafe>(FTerrainWorldFieldParams());
    S->ActiveInit.DensityField=S->DensityField.Get();
    S->ActiveInit.DensityFieldOwner=S->DensityField;
    S->Backend=MakeUnique<FMemoryTerrainBackend>();
    TestTrue(TEXT("memory fixture initializes"),S->Backend->Initialize(S->ActiveInit));
    FTerrainStreamingInterest Interest; Interest.InterestId=1; Interest.RadiusCm=5000;
    S->Backend->SetStreamingInterest(Interest); S->State=ETerrainServiceState::Ready;
    FTerrainOp Op; Op.Kind=ETerrainOpKind::Remove; Op.CentreVox=FIntVector(8); Op.RadiusVoxQ16=2*65536; Op.OpSeq=1;
    TArray<uint8> Bytes; SerializeTerrainOp(Op,Bytes);
    FTerrainChunkRevision Rev; Rev.Key=FIntVector(0); Rev.Before=0; Rev.After=1;
    TArray<FTerrainChunkRevision> Revs={Rev};
    const FTerrainChunkKey Key(0,0,0);
    const uint64 Initial=S->HashChunk(Key);
    TestTrue(TEXT("resident initial hash"),Initial!=0);
    // P-008 §3: checks apply to SYNCED chunks. Announce the chunk pristine first.
    TestTrue(TEXT("pristine chunk accepted"),S->AcceptPristine({FIntVector(0)}));
    TArray<FIntVector> Resync;
    auto Gap=Revs; Gap[0].Before=1; Gap[0].After=2;
    TestTrue(TEXT("a gapped op is still a usable op"),S->ApplyReplicatedOp(Bytes,Gap,Resync));
    TestTrue(TEXT("missing previous revision is reported for resync"),Resync.Num()==1 && Resync[0]==FIntVector(0));
    TestEqual(TEXT("a lost chunk's revision is not advanced"),S->GetRevision(Key),uint32(0));
    TestFalse(TEXT("cannot acknowledge corrupted chunk pristine"),S->AcceptPristine({FIntVector(0)}));
    TestTrue(TEXT("later op still applies"),S->ApplyReplicatedOp(Bytes,Revs,Resync));
    TestEqual(TEXT("but an unsynced chunk is neither checked nor bumped"),S->GetRevision(Key),uint32(0));
    TestEqual(TEXT("and is not reported twice"),Resync.Num(),0);
    // The repair: an authoritative snapshot of the chunk at revision 1.
    FMemoryTerrainBackend Truth; Truth.Initialize(S->ActiveInit); Truth.SetStreamingInterest(Interest);
    FTerrainEditResult TruthResult; Truth.ApplyOp(Op,TruthResult);
    FTerrainRegionData Region; Truth.ReadRegion(Key,Region);
    TArray<uint8> Compressed; TestTrue(TEXT("snapshot encodes"),TerrainEncodeChunkSnapshot(Region.Payload,Compressed));
    TestTrue(TEXT("snapshot repairs the chunk"),S->ApplyReplicatedSnapshot(Key,1,Compressed));
    TestEqual(TEXT("revision taken from the snapshot"),S->GetRevision(Key),uint32(1));
    const uint64 Edited=S->HashChunk(Key);
    TestEqual(TEXT("data taken from the snapshot"),Edited,Truth.HashRegion(Key));
    TestTrue(TEXT("real mutation"),Edited!=Initial);
    TestTrue(TEXT("duplicate replay is usable"),S->ApplyReplicatedOp(Bytes,Revs,Resync));
    TestEqual(TEXT("duplicate replay is detected as a gap"),Resync.Num(),1);
    TestEqual(TEXT("duplicate preserves terrain (idempotent)"),S->HashChunk(Key),Edited);
    TestEqual(TEXT("duplicate does not advance the revision"),S->GetRevision(Key),uint32(1));
    Revs[0].Before=1; Revs[0].After=2; const auto Duplicate=Revs[0]; Revs.Add(Duplicate);
    TestFalse(TEXT("extra revision entry refused"),S->ApplyReplicatedOp(Bytes,Revs,Resync));
    TestEqual(TEXT("malformed envelope preserves terrain"),S->HashChunk(Key),Edited);
    FTerrainEditRequest Request; FTerrainOp Quantised;
    Request.RadiusCm=-1;
    TestTrue(TEXT("negative radius refused"),S->QuantiseRequest(Request,Quantised)==ETerrainEditRejection::BadRequest);
    Request.RadiusCm=200; Request.WorldLocation=FVector(1.e100,0,0);
    TestTrue(TEXT("overflow input refused"),S->QuantiseRequest(Request,Quantised)==ETerrainEditRejection::OutOfBounds);
    FTerrainSourceState Source; Source.Position=FVector(1.e6);
    Op.Source=ETerrainSource::Player;
    TestTrue(TEXT("authoritative reach enforced"),S->ValidateOp(Op,Source)==ETerrainEditRejection::OutOfReach);
    Source.bPermitted=false;
    TestTrue(TEXT("permission enforced"),S->ValidateOp(Op,Source)==ETerrainEditRejection::PermissionDenied);
    S->Deinitialize();
    return true;
}
#endif

// Copyright VoxelWorld. In-world density contract checks; materials/yield are step 6.
#include "TerrainService.h"
#include "TerrainCore.h"
#include "TerrainChunk.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING
void UTerrainService::RunAdapterChecks()
{
    if (!IsBackendReady() || !HasAuthority() || GetWorld()->GetNetMode()!=NM_Standalone) return;
    int32 Failures=0;
    const auto Check=[&](bool Good,const TCHAR* What)
    { if (!Good) { ++Failures; UE_LOG(LogTerrainCore,Error,TEXT("Adapter.DensityContract: %s"),What); } };
    // Fixed solid chunk and negative coordinates; restore the captured baseline after testing.
    const FTerrainChunkKey Key(-6,0,-1);
    FTerrainRegionData Baseline;
    if (!Backend->ReadRegion(Key,Baseline)) { UE_LOG(LogTerrainCore,Error,TEXT("Adapter.DensityContract: cannot capture fixture")); return; }
    FTerrainRegionData Solid=Baseline;
    for (int32 I=0;I<TerrainChunkSampleCount;++I) { Solid.Payload[2*I]=1; Solid.Payload[2*I+1]=128; }
    const auto Restore=[&]() { Check(Backend->WriteRegion(Solid),TEXT("restore solid fixture")); };
    FTerrainOp Dig; Dig.Kind=ETerrainOpKind::Remove; Dig.CentreVox=FIntVector(-176,16,-16); Dig.RadiusVoxQ16=4*65536;
    FTerrainEditResult Result;
    // Recorded once on UE 5.8.2 / Legacy 434 after canonical W/cell correction.
    // A future failure requires kernel/version investigation, never fixture regeneration.
    const TArray<uint64> Expected={531579193041328928ULL,17368436259738063436ULL,
        16484604156930415915ULL,10719771348379707673ULL};
    for (int32 Run=0;Run<20;++Run)
    {
        Restore();
        TArray<uint64> Actual;
        for (int32 Step=0;Step<4;++Step)
        {
            auto Op=Dig;
            Op.Kind=Step%2 ? ETerrainOpKind::Add : ETerrainOpKind::Remove;
            Op.RadiusVoxQ16=(Step%2 ? 2 : 4)*65536;
            Op.CentreVox.X += Step/2;
            Check(Backend->ApplyOp(Op,Result),TEXT("script applies"));
            Check(!Result.bTruncated,TEXT("no partial result"));
            const uint64 Hash=Backend->HashRegion(Key); Actual.Add(Hash);
            Check(Hash!=0,TEXT("resident hash nonzero"));
            Check(Backend->ApplyOp(Op,Result) && Result.VoxelsTouched==0,TEXT("repeat is idempotent"));
            Check(Backend->HashRegion(Key)==Hash,TEXT("repeat preserves hash"));
        }
        Check(Expected==Actual,TEXT("twenty replays match pinned density fixtures"));
    }
    for (int32 I=0;I<Expected.Num();++I)
        UE_LOG(LogTerrainCore,Display,TEXT("Adapter.DensityContract fixture[%d]=%llu"),I,Expected[I]);
    Restore();
    Check(Backend->ApplyOp(Dig,Result),TEXT("geometry probe applies"));
    FTerrainRegionData After; Check(Backend->ReadRegion(Key,After),TEXT("capture geometry probe"));
    if (After.Payload.Num()==Solid.Payload.Num())
    {
        const auto B=TerrainChunkBounds(Key);
        int32 OutsideChanges=0,UnfilledCells=0,NonMonotone=0;
        for (int32 Z=0;Z<32;++Z) for (int32 Y=0;Y<32;++Y) for (int32 X=0;X<32;++X)
        {
            const int32 I=X+32*Y+1024*Z;
            const FIntVector P=B.Min+FIntVector(X,Y,Z);
            const int16 Value=int16(uint16(After.Payload[I*2]) | uint16(After.Payload[I*2+1])<<8);
            const FIntVector D=P-Dig.CentreVox;
            const int64 Dist=int64(D.X)*D.X+int64(D.Y)*D.Y+int64(D.Z)*D.Z;
            if (Dist>16 && Value!=-32767) ++OutsideChanges;
            int64 Far=0;
            for (int32 A=0;A<3;++A) Far+=FMath::Max(int64(D[A])*D[A],int64(D[A]+1)*(D[A]+1));
            if (Far<=16 && Value!=32767) ++UnfilledCells;
            if (Value < -32767) ++NonMonotone;
        }
        Check(OutsideChanges==0,TEXT("no sample outside canonical W changes"));
        Check(UnfilledCells==0,TEXT("whole cells inside sphere become empty"));
        Check(NonMonotone==0,TEXT("remove is monotone"));
    }
    const uint64 BeforeRefusal=Backend->HashRegion(Key);
    auto Invalid=Dig; Invalid.RadiusVoxQ16=100*65536;
    Check(!Backend->ApplyOp(Invalid,Result),TEXT("oversized sphere refused"));
    Check(Backend->HashRegion(Key)==BeforeRefusal,TEXT("refusal preserves hash"));
    Invalid=Dig; Invalid.CentreVox=FIntVector(MAX_int32);
    Check(!Backend->ApplyOp(Invalid,Result),TEXT("overflow refused"));
    Check(Backend->HashRegion(Key)==BeforeRefusal,TEXT("overflow preserves hash"));
    Invalid=Dig; Invalid.Kind=ETerrainOpKind::Smooth;
    Check(!Backend->ApplyOp(Invalid,Result),TEXT("reserved operation refused"));
    Check(Backend->HashRegion(Key)==BeforeRefusal,TEXT("reserved refusal preserves hash"));
    Check(Backend->WriteRegion(Baseline),TEXT("restore original world"));
    UE_LOG(LogTerrainCore,Display,TEXT("**** Adapter.DensityContract: %s runs=20 failures=%d ****"),Failures ? TEXT("FAIL") : TEXT("PASS"),Failures);
}
static FAutoConsoleCommandWithWorld GAdapterChecks(TEXT("Terrain.AdapterChecks"),
    TEXT("Standalone development: twenty replay runs, exact write set, idempotence and atomic refusal; restores fixture."),
    FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
    {
        if (!World || World->GetNetMode()!=NM_Standalone) return;
        FTimerHandle Handle; TWeakObjectPtr<UWorld> Weak(World);
        World->GetTimerManager().SetTimer(Handle,FTimerDelegate::CreateLambda([Weak]()
        { if (auto* W=Weak.Get()) if (auto* S=W->GetSubsystem<UTerrainService>()) S->RunAdapterChecks(); }),3.f,false);
    }));
#endif

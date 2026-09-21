// Copyright VoxelWorld. In-world density contract checks, plus materials and yield (P-009).
#include "TerrainService.h"
#include "TerrainCore.h"
#include "TerrainChunk.h"
#include "TerrainMaterials.h"
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
    // ===== P-009: materials and yield, on the real plugin ===================================
    {
        // Generated terrain reads back as known game materials, and QueryPoint agrees.
        int32 UnknownSamples=0; TSet<FTerrainMatId> Seen;
        for (int32 I=0;I<TerrainChunkSampleCount;++I)
        {
            const FTerrainMatId Id=FTerrainMatId(uint16(Baseline.Payload[(TerrainChunkSampleCount+I)*2]) | uint16(Baseline.Payload[(TerrainChunkSampleCount+I)*2+1])<<8);
            if (Id==ETerrainMaterial::Unknown || Id>=ETerrainMaterial::Count) ++UnknownSamples; else Seen.Add(Id);
        }
        Check(UnknownSamples==0,TEXT("Yield: every generated sample reads back a catalog material"));
        const auto Bounds=TerrainChunkBounds(Key);
        FTerrainPointSample Point; Backend->QueryPoint(Bounds.Min+FIntVector(5,7,9),Point);
        const int32 PointIndex=5+32*7+1024*9;
        const FTerrainMatId Expected9=FTerrainMatId(uint16(Baseline.Payload[(TerrainChunkSampleCount+PointIndex)*2]) | uint16(Baseline.Payload[(TerrainChunkSampleCount+PointIndex)*2+1])<<8);
        Check(Point.MaterialId==Expected9,TEXT("Yield: QueryPoint and ReadRegion agree on material"));
        FString Names; for (FTerrainMatId Id:Seen) Names+=FString(TerrainMaterialName(Id))+TEXT(" ");
        UE_LOG(LogTerrainCore,Display,TEXT("Adapter.Yield fixture chunk materials: %s"),*Names);

        // A fixture of one material, or two split at local z=16.
        const auto Fixture=[&](FTerrainMatId Lower,FTerrainMatId Upper)
        {
            FTerrainRegionData F=Solid;
            for (int32 I=0;I<TerrainChunkSampleCount;++I)
            {
                const FTerrainMatId Id=(I/1024)<16 ? Lower : Upper;
                F.Payload[(TerrainChunkSampleCount+I)*2]=uint8(Id&0xFF); F.Payload[(TerrainChunkSampleCount+I)*2+1]=uint8(Id>>8);
            }
            Check(Backend->WriteRegion(F),TEXT("Yield: write fixture"));
            return F;
        };
        const auto Sum=[](const FTerrainEditResult& R,FTerrainMatId Id){ int64 V=0; for (const auto& E:R.Removed) if (E.MaterialId==Id) V+=E.MicroLitres; return V; };
        const auto Total=[](const FTerrainEditResult& R){ int64 V=0; for (const auto& E:R.Removed) V+=E.MicroLitres; return V; };

        // Round trip: what WriteRegion stores is what ReadRegion returns, both halves.
        const FTerrainRegionData Stone=Fixture(ETerrainMaterial::Stone,ETerrainMaterial::Stone);
        FTerrainRegionData Back; Backend->ReadRegion(Key,Back);
        Check(Back.Payload==Stone.Payload,TEXT("Yield: WriteRegion/ReadRegion round-trip density and material exactly"));

        // E-1: is the occupancy sum the volume the player actually sees removed? The truth is the
        // rendered hole: the region where the trilinearly interpolated post-edit density is empty
        // (> 0), which is what an isosurface mesher draws. Supersampled 4x4x4 per cell here,
        // independently of the occupancy function under test. The ideal sphere 4/3 pi R^3 is logged
        // for scale only: the canonical write set (§4.10.2) clips the dig, so the hole is smaller.
        const double Unit=double(GetVoxelSizeCm())*GetVoxelSizeCm()*GetVoxelSizeCm()*1000.0;
        const auto Rendered=[&](int32 R)
        {
            FTerrainRegionData Now; Backend->ReadRegion(Key,Now);
            const auto D=[&](int32 X,int32 Y,int32 Z)
            { const int32 I=X+32*Y+1024*Z; return double(int16(uint16(Now.Payload[I*2]) | uint16(Now.Payload[I*2+1])<<8))/32767.0; };
            int64 EmptySub=0;
            for (int32 Z=16-R-2;Z<16+R+2;++Z) for (int32 Y=16-R-2;Y<16+R+2;++Y) for (int32 X=16-R-2;X<16+R+2;++X)
            for (int32 SZ=0;SZ<4;++SZ) for (int32 SY=0;SY<4;++SY) for (int32 SX=0;SX<4;++SX)
            {
                const double FX=(SX+.5)/4, FY=(SY+.5)/4, FZ=(SZ+.5)/4;
                double V=0;
                for (int32 C=0;C<8;++C)
                {
                    const int32 CX=C&1, CY=(C>>1)&1, CZ=(C>>2)&1;
                    V+=D(X+CX,Y+CY,Z+CZ)*(CX?FX:1-FX)*(CY?FY:1-FY)*(CZ?FZ:1-FZ);
                }
                if (V>0) ++EmptySub;
            }
            return double(EmptySub)*Unit/64.0;
        };
        for (const int32 R:{3,4,6,8})
        {
            Fixture(ETerrainMaterial::Stone,ETerrainMaterial::Stone);
            FTerrainOp Op=Dig; Op.RadiusVoxQ16=R*65536; Op.CentreVox=FIntVector(-176,16,-16);
            FTerrainEditResult Out; Check(Backend->ApplyOp(Op,Out),TEXT("Yield: E-1 dig applies"));
            const double Measured=double(Sum(Out,ETerrainMaterial::Stone));
            const double Truth=Rendered(R);
            const double Ideal=4.0/3.0*PI*R*R*R*Unit;
            UE_LOG(LogTerrainCore,Display,TEXT("Adapter.Yield E-1 R=%d: measured %.3f L, rendered hole %.3f L (ratio %.4f); ideal sphere %.3f L (ratio %.4f); touched %lld"),
                R,Measured/1.0e6,Truth/1.0e6,Measured/Truth,Ideal/1.0e6,Measured/Ideal,Out.VoxelsTouched);
            Check(Out.Removed.Num()==1 && Out.Removed[0].MaterialId==ETerrainMaterial::Stone,TEXT("Yield: homogeneous dig yields exactly one material"));
            Check(Truth>0 && FMath::Abs(Measured/Truth-1.0)<0.10,TEXT("Yield: E-1 measured volume within 10% of the rendered hole"));
            FTerrainEditResult Again; Backend->ApplyOp(Op,Again);
            Check(Again.VoxelsTouched==0 && Again.Removed.Num()==0,TEXT("Yield: repeating a dig yields nothing"));
        }

        // Mixed geology: the same dig across a stone/ore boundary splits the same total.
        FTerrainOp Mixed=Dig; Mixed.RadiusVoxQ16=6*65536; Mixed.CentreVox=FIntVector(-176,16,-16);
        Fixture(ETerrainMaterial::Stone,ETerrainMaterial::Stone);
        FTerrainEditResult Homogeneous; Backend->ApplyOp(Mixed,Homogeneous);
        Fixture(ETerrainMaterial::Stone,ETerrainMaterial::IronOre);
        FTerrainEditResult Split; Backend->ApplyOp(Mixed,Split);
        UE_LOG(LogTerrainCore,Display,TEXT("Adapter.Yield mixed: stone %.3f L + iron ore %.3f L = %.3f L (homogeneous %.3f L)"),
            Sum(Split,ETerrainMaterial::Stone)/1.0e6,Sum(Split,ETerrainMaterial::IronOre)/1.0e6,Total(Split)/1.0e6,Total(Homogeneous)/1.0e6);
        Check(Sum(Split,ETerrainMaterial::Stone)>0 && Sum(Split,ETerrainMaterial::IronOre)>0,TEXT("Yield: a boundary dig yields both materials"));
        Check(FMath::Abs(Total(Split)-Total(Homogeneous))<=2,TEXT("Yield: splitting by material conserves the total (rounding only)"));

        // Place then mine: Add with a material paints it and is measured as negative; digging
        // it back out yields that material.
        FTerrainOp Place=Mixed; Place.Kind=ETerrainOpKind::Add; Place.RadiusVoxQ16=4*65536; Place.MaterialId=ETerrainMaterial::Dirt;
        FTerrainEditResult Placed; Check(Backend->ApplyOp(Place,Placed),TEXT("Yield: placement applies"));
        Check(Placed.Removed.Num()==1 && Placed.Removed[0].MaterialId==ETerrainMaterial::Dirt && Placed.Removed[0].MicroLitres<0,
            TEXT("Yield: placement is measured as negative volume of the placed material"));
        FTerrainPointSample Centre; Backend->QueryPoint(Mixed.CentreVox,Centre);
        Check(Centre.MaterialId==ETerrainMaterial::Dirt,TEXT("Yield: placement paints the placed material"));
        FTerrainOp Mine=Place; Mine.Kind=ETerrainOpKind::Remove; Mine.MaterialId=0;
        FTerrainEditResult Mined; Backend->ApplyOp(Mine,Mined);
        UE_LOG(LogTerrainCore,Display,TEXT("Adapter.Yield place/mine: placed %.3f L dirt, mined back %.3f L dirt"),
            -Sum(Placed,ETerrainMaterial::Dirt)/1.0e6,Sum(Mined,ETerrainMaterial::Dirt)/1.0e6);
        // The DEF-6 physical guard: digging a placement back out must never yield MORE than was
        // placed, or add-then-mine mints material. Less is a leak worth knowing, not a mint.
        Check(Sum(Mined,ETerrainMaterial::Dirt)>0 && Sum(Mined,ETerrainMaterial::Dirt)<=-Sum(Placed,ETerrainMaterial::Dirt),
            TEXT("Yield: mining a placement back never yields more than was placed"));

        // Placement with no material leaves the existing material alone.
        FTerrainOp Blank=Place; Blank.MaterialId=0;
        Fixture(ETerrainMaterial::Stone,ETerrainMaterial::Stone);
        FTerrainEditResult Hole; Backend->ApplyOp(Mine,Hole);
        FTerrainEditResult Filled; Backend->ApplyOp(Blank,Filled);
        Backend->QueryPoint(Mixed.CentreVox,Centre);
        Check(Centre.MaterialId==ETerrainMaterial::Stone,TEXT("Yield: an unmaterialled placement repaints nothing"));
    }
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

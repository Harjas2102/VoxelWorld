// Copyright VoxelWorld. P-002: exact partitions, fixed format, bounded work.
#if WITH_DEV_AUTOMATION_TESTS
#include "TerrainOpGeometry.h"
#include "TerrainChunk.h"
#include "MemoryTerrainBackend.h"
#include "ITerrainDensityField.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainSplitTest,"TerrainCore.Split.Equivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext
	| EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

bool FTerrainSplitTest::RunTest(const FString&)
{
	FTerrainOp Box; Box.Shape=ETerrainShape::Box; Box.ExtentVox=FIntVector(21);
	TArray<FTerrainOp> Parts;
	TestTrue(TEXT("42-cubed counterexample splits within fixed format"),SplitTerrainOp(Box,65536,16,Parts));
	TestEqual(TEXT("Counterexample needs two parts"),Parts.Num(),2);
	if (Parts.Num()!=2) return false;
	TestEqual(TEXT("Tie selects X, lower balanced cut"),Parts[0].CentreVox,FIntVector(-11,0,0));
	TestEqual(TEXT("Lower extent is representable"),Parts[0].ExtentVox,FIntVector(10,21,21));
	TestEqual(TEXT("Upper centre"),Parts[1].CentreVox,FIntVector(10,0,0));
	TSet<FIntVector> Visited;
	for (const auto& P:Parts)
	{
		FTerrainBox B; TerrainOpBounds(P,B); int64 W,Scans;
		TestTrue(TEXT("Each child under the cap"),TerrainOpCounts(P,65536,W,Scans));
		for (int32 Z=B.Min.Z;Z<B.Max.Z;++Z) for (int32 Y=B.Min.Y;Y<B.Max.Y;++Y) for (int32 X=B.Min.X;X<B.Max.X;++X)
		{
			const FIntVector V(X,Y,Z);
			if (Visited.Contains(V) || !TerrainOpContains(Box,V)) { AddError(TEXT("Partition overlap or escaped parent")); return false; }
			Visited.Add(V);
		}
	}
	TestEqual(TEXT("Exact whole-box voxel count"),Visited.Num(),74088);
	TestFalse(TEXT("Capacity rejection is atomic"),SplitTerrainOp(Box,65536,1,Parts));
	TestTrue(TEXT("No partial subdivision escapes"),Parts.IsEmpty());
	TestFalse(TEXT("Cap below minimum box rejects"),SplitTerrainOp(Box,7,256,Parts));
	Box.CentreVox=FIntVector(MAX_int32);
	TestFalse(TEXT("Overflowing bounds reject before arithmetic wraps"),SplitTerrainOp(Box,65536,256,Parts));
	FTerrainOp Sphere; Sphere.RadiusVoxQ16=4*65536;
	int64 Writes=0,Scans=0;
	TestTrue(TEXT("Exact integer sphere count"),TerrainOpCounts(Sphere,65536,Writes,Scans));
	TestEqual(TEXT("r4 has 257 lattice samples"),Writes,int64(257));
	TestEqual(TEXT("r4 read cube has 729 positions"),Scans,int64(729));
	TestFalse(TEXT("Over-cap sphere never splits"),SplitTerrainOp(Sphere,256,256,Parts));

	struct FField : ITerrainDensityField { FTerrainDensitySample Sample(FIntVector) const override { return {-1,17}; } } Field;
	for (const auto Kind:{ETerrainOpKind::Remove,ETerrainOpKind::Add,ETerrainOpKind::Paint})
	for (const FIntVector Centre:{FIntVector(-3,0,2),FIntVector(0),FIntVector(16,12,9)})
	{
		FMemoryTerrainBackend Whole,Split;
		FTerrainBackendInit Init; Init.WorldBoundsVox=FTerrainBox(FIntVector(-32),FIntVector(64)); Init.DensityField=&Field;
		Whole.Initialize(Init); Split.Initialize(Init);
		FTerrainStreamingInterest Interest; Interest.InterestId=1; Interest.RadiusCm=6000;
		Whole.SetStreamingInterest(Interest); Split.SetStreamingInterest(Interest);
		FTerrainOp Op; Op.Shape=ETerrainShape::Box; Op.ExtentVox=FIntVector(7,9,11); Op.CentreVox=Centre;
		Op.Kind=Kind; Op.MaterialId=41;
		FTerrainEditResult R;
		if (Kind==ETerrainOpKind::Add)
		{
			FTerrainOp Empty=Op; Empty.Kind=ETerrainOpKind::Remove;
			Whole.ApplyOp(Empty,R); Split.ApplyOp(Empty,R);
		}
		TestTrue(TEXT("Whole reference op succeeds"),Whole.ApplyOp(Op,R));
		TestTrue(TEXT("Recursive representable partition"),SplitTerrainOp(Op,128,256,Parts));
		for (const auto& P:Parts) TestTrue(TEXT("Split child applies"),Split.ApplyOp(P,R));
		FTerrainBox B; TerrainOpBounds(Op,B); TArray<FTerrainChunkKey> Keys; TerrainChunkKeysForBox(B,Keys);
		for (const auto& K:Keys) TestEqual(TEXT("Whole and split densities/materials hash identically"),Split.HashRegion(K),Whole.HashRegion(K));
		Whole.Shutdown(); Split.Shutdown();
	}
	return true;
}
#endif

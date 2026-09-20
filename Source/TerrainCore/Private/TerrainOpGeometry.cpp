// Copyright VoxelWorld.
#include "TerrainOpGeometry.h"

bool TerrainOpBounds(const FTerrainOp& Op, FTerrainBox& Out)
{
	Out = {};
	if ((Op.Kind != ETerrainOpKind::Remove && Op.Kind != ETerrainOpKind::Add && Op.Kind != ETerrainOpKind::Paint)
		|| (Op.Shape != ETerrainShape::Sphere && Op.Shape != ETerrainShape::Box)
		|| (Op.Shape == ETerrainShape::Sphere && Op.RadiusVoxQ16 <= 0)) return false;
	FTerrainBox Bounds;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const int64 E = Op.Shape == ETerrainShape::Sphere ? Op.RadiusVoxQ16 / 65536 : Op.ExtentVox[Axis];
		if (Op.Shape == ETerrainShape::Box && E <= 0) return false;
		const int64 Low = int64(Op.CentreVox[Axis]) - E;
		const int64 High = int64(Op.CentreVox[Axis]) + E + (Op.Shape == ETerrainShape::Sphere ? 1 : 0);
		if (Low < MIN_int32 || High > MAX_int32 || High <= Low) return false;
		Bounds.Min[Axis] = int32(Low);
		Bounds.Max[Axis] = int32(High);
	}
	Out = Bounds;
	return true;
}

bool TerrainOpContains(const FTerrainOp& Op, const FIntVector& P)
{
	if (Op.Shape == ETerrainShape::Box)
	{
		for (int32 A = 0; A < 3; ++A)
			if (int64(P[A]) < int64(Op.CentreVox[A]) - Op.ExtentVox[A]
				|| int64(P[A]) >= int64(Op.CentreVox[A]) + Op.ExtentVox[A]) return false;
		return true;
	}
	const double R = double(Op.RadiusVoxQ16) / 65536.0;
	const double X = double(P.X) - Op.CentreVox.X, Y = double(P.Y) - Op.CentreVox.Y, Z = double(P.Z) - Op.CentreVox.Z;
	return X*X + Y*Y + Z*Z <= R*R;
}

bool TerrainOpCounts(const FTerrainOp& Op, int64 MaxWrites, int64& Writes, int64& Scans)
{
	Writes = Scans = 0;
	FTerrainBox Bounds;
	if (MaxWrites < 1 || MaxWrites > 65536 || !TerrainOpBounds(Op, Bounds)) return false;
	int64 Count = 1;
	for (int32 A = 0; A < 3; ++A)
	{
		const int64 Width = int64(Bounds.Max[A]) - Bounds.Min[A];
		// A cube around a legal sphere at this cap is below this read-work ceiling.
		if (Width > 262144 || Count > 262144 / Width) return false;
		Count *= Width;
	}
	int64 Written = 0;
	if (Op.Shape == ETerrainShape::Box) Written = Count;
	else
	{
		for (int32 Z = Bounds.Min.Z; Z < Bounds.Max.Z; ++Z)
		for (int32 Y = Bounds.Min.Y; Y < Bounds.Max.Y; ++Y)
		for (int32 X = Bounds.Min.X; X < Bounds.Max.X; ++X)
			if (TerrainOpContains(Op, FIntVector(X,Y,Z)) && ++Written > MaxWrites) return false;
	}
	if (Written > MaxWrites) return false;
	Writes = Written; Scans = Count;
	return true;
}

bool SplitTerrainOp(const FTerrainOp& Op, int64 MaxWrites, int32 MaxParts, TArray<FTerrainOp>& Out)
{
	Out.Reset();
	FTerrainBox Root;
	if (MaxParts < 1 || MaxParts > 256 || MaxWrites < 1 || MaxWrites > 65536 || !TerrainOpBounds(Op, Root)) return false;
	if (Op.Shape == ETerrainShape::Sphere)
	{
		int64 W, B;
		if (!TerrainOpCounts(Op, MaxWrites, W, B)) return false;
		Out.Add(Op); return true;
	}
	if (MaxWrites < 8) return false;
	// Check total volume against capacity without overflowing or constructing a tree.
	int64 Volume = 1;
	const int64 Capacity = MaxWrites * MaxParts;
	for (int32 A = 0; A < 3; ++A)
	{
		const int64 Width = int64(Root.Max[A]) - Root.Min[A];
		if (Volume > Capacity / Width) return false;
		Volume *= Width;
	}
	TArray<FTerrainOp> Pending{ Op }, Parts;
	while (!Pending.IsEmpty())
	{
		FTerrainOp Part = Pending.Pop(EAllowShrinking::No);
		const int64 V = int64(Part.ExtentVox.X)*Part.ExtentVox.Y*Part.ExtentVox.Z*8;
		if (V <= MaxWrites) { Parts.Add(Part); continue; }
		if (Pending.Num() + Parts.Num() + 2 > MaxParts) return false;
		int32 Axis = 0;
		for (int32 A = 1; A < 3; ++A) if (Part.ExtentVox[A] > Part.ExtentVox[Axis]) Axis = A;
		const int32 E = Part.ExtentVox[Axis];
		if (E < 2) return false;
		FTerrainOp Lower = Part, Upper = Part;
		Lower.ExtentVox[Axis] = E / 2;
		Upper.ExtentVox[Axis] = E - E / 2;
		Lower.CentreVox[Axis] = int32(int64(Part.CentreVox[Axis]) - E + Lower.ExtentVox[Axis]);
		Upper.CentreVox[Axis] = int32(int64(Part.CentreVox[Axis]) + E - Upper.ExtentVox[Axis]);
		Pending.Add(Upper); Pending.Add(Lower);
	}
	Out = MoveTemp(Parts);
	return true;
}

// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#include "CoreMinimal.h"
#include "ITerrainDensityField.h"
#include "TerrainMaterials.h"
#include "VoxelGenerators/VoxelGenerator.h"
#include "VoxelGenerators/VoxelGeneratorHelpers.h"
#include "VPLegacyDensityGenerator.generated.h"

/**
 * UVPLegacyDensityGenerator — ARCHITECTURE.md §4.6's adapter half of T-108.
 *
 *   "UVPLegacyDensityGenerator : UVoxelGenerator (adapter) implements the plugin's value and
 *    material queries by forwarding iteration to the field."
 *
 * That is the entire job. This class decides NOTHING about the world: not a height, not a
 * stratum, not an ore body. It converts a plugin query into an ITerrainDensityField query and
 * a game answer into a plugin answer. The shape lives in FTerrainWorldField, in TerrainCore,
 * where it compiles without the plugin and unit-tests without an engine — which is the point
 * §4.6 makes about the Pro gate being leverage rather than only a cost.
 *
 * PRIVATE ON PURPOSE, like FVPLegacyBackend: this header names UVoxelGenerator, so a module
 * that could include it would acquire the plugin through it (§4.1). Nothing outside this
 * module refers to this class.
 *
 * COORDINATES. The plugin queries in the voxel world actor's own voxel space, and
 * ConformVoxelWorld forces that actor onto the terrain origin, voxel size and extent the
 * service quantises against (AR-5, §8.1). So plugin voxel coordinates ARE game voxel
 * coordinates, one to one, with no conversion here — the same identity FVPLegacyBackend
 * already relies on when it hands a game FIntVector straight to UVoxelDataTools::GetValue.
 * If that ever stopped being true, this file and that one would both be wrong, together.
 *
 * VALUE SIGN. Both sides agree: FVoxelValue::Full() is -1 and FVoxelValue::Empty() is +1, and
 * FTerrainDensitySample documents "negative is solid". No inversion is needed and none is
 * applied.
 *
 * LIFETIME. The immutable field is created by UTerrainService. This UObject and each
 * generator instance retain shared ownership, so outstanding plugin workers may finish
 * after service teardown without dereferencing freed memory or waiting on the game thread.
 */
UCLASS()
class UVPLegacyDensityGenerator : public UVoxelGenerator
{
	GENERATED_BODY()

public:
	/** Must be called before the world is created or recreated. The shared field lifetime is retained. */
	void SetField(const ITerrainDensityField* InField, TSharedPtr<const ITerrainDensityField, ESPMode::ThreadSafe> InOwner = {})
	{ Field = InField; FieldOwner = MoveTemp(InOwner); }
	TSharedPtr<const ITerrainDensityField, ESPMode::ThreadSafe> GetFieldOwner() const { return FieldOwner; }

	const ITerrainDensityField* GetField() const { return Field; }

	//~ Begin UVoxelGenerator Interface
	virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance() override;
	//~ End UVoxelGenerator Interface

private:
	/** Raw and not a UPROPERTY because it is not a UObject. See LIFETIME above. */
	const ITerrainDensityField* Field = nullptr;
	TSharedPtr<const ITerrainDensityField, ESPMode::ThreadSafe> FieldOwner;
};

/**
 * The strata palette. COSMETIC, AND NOT THE K9 MAPPING.
 *
 * Fork K9 — "game FTerrainMatId maps to a per-voxel plugin material" — is build step 6, and
 * DEF-6 (yield conservation and placement policy) is open. This table exists so the Director
 * can SEE topsoil, dirt, stone and an ore body as distinct bands in the cliff face, which is
 * the only way to check by eye that the strata are where the field says they are. Nothing
 * reads a colour back, nothing persists one, and no yield is computed from one. When K9 lands
 * it replaces this with a real material catalog and this function goes away.
 */
FLinearColor TerrainMaterialDebugColor(FTerrainMatId Id);

class FVPLegacyDensityGeneratorInstance
	: public TVoxelGeneratorInstanceHelper<FVPLegacyDensityGeneratorInstance, UVPLegacyDensityGenerator>
{
public:
	using Super = TVoxelGeneratorInstanceHelper<FVPLegacyDensityGeneratorInstance, UVPLegacyDensityGenerator>;

	explicit FVPLegacyDensityGeneratorInstance(UVPLegacyDensityGenerator& Object)
		: Super(&Object)
		, Field(Object.GetField())
		, FieldOwner(Object.GetFieldOwner())
	{
	}

	//~ Begin FVoxelGeneratorInstance Interface
	FORCEINLINE v_flt GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
	{
		if (!Field)
		{
			// Empty rather than full: a missing field must not bury the player inside rock.
			return 1;
		}
		return Field->Sample(ToVoxel(X, Y, Z)).Density;
	}

	FORCEINLINE FVoxelMaterial GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
	{
		if (!Field)
		{
			return FVoxelMaterial::Default();
		}
		return FVoxelMaterial::CreateFromColor(
			TerrainMaterialDebugColor(Field->Sample(ToVoxel(X, Y, Z)).MaterialId));
	}

	/**
	 * The octree asks this before it asks for values, and a region whose range is entirely
	 * positive or entirely negative is skipped without ever being sampled. AR-6 added
	 * ITerrainDensityField::SampleRange precisely so the game can answer it; the default
	 * implementation returns [-1, 1], which is correct and simply forfeits every skip.
	 *
	 * FVoxelIntBox and FTerrainBox agree that Min is inclusive and Max exclusive, so this is
	 * a copy and not a conversion.
	 */
	TVoxelRange<v_flt> GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const
	{
		if (!Field)
		{
			return TVoxelRange<v_flt>(1, 1);
		}
		const FTerrainDensityRange Range = Field->SampleRange(FTerrainBox(Bounds.Min, Bounds.Max));
		const v_flt Min = FMath::Min<v_flt>(Range.Min, Range.Max);
		const v_flt Max = FMath::Max<v_flt>(Range.Min, Range.Max);
		return TVoxelRange<v_flt>(Min, Max);
	}

	virtual FVector GetUpVector(v_flt X, v_flt Y, v_flt Z) const override final
	{
		return FVector::UpVector;
	}
	//~ End FVoxelGeneratorInstance Interface

private:
	/**
	 * The plugin queries on the integer lattice but hands the coordinates over as v_flt.
	 * ITerrainDensityField takes integers by R-011's determination, so round rather than
	 * truncate: truncation is asymmetric about zero and would shift the world half a voxel
	 * for negative coordinates only, which is the kind of error that looks like a seam.
	 */
	FORCEINLINE static FIntVector ToVoxel(v_flt X, v_flt Y, v_flt Z)
	{
		return FIntVector(
			FMath::RoundToInt(static_cast<double>(X)),
			FMath::RoundToInt(static_cast<double>(Y)),
			FMath::RoundToInt(static_cast<double>(Z)));
	}

	const ITerrainDensityField* Field = nullptr;
	// Plugin tasks can outlive actor destruction. This immutable owner survives with the instance.
	TSharedPtr<const ITerrainDensityField, ESPMode::ThreadSafe> FieldOwner;
};

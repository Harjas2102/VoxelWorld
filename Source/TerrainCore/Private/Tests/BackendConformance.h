// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.

#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "ITerrainBackend.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"

class FAutomationTestBase;

/**
 * Reusable contract tests (ARCHITECTURE.md 6.1 / 10). An adapter's test registration
 * includes this header and passes its own factory; no edits to this suite are needed.
 * The factory returns a fresh, uninitialised backend. Tests provide explicit Dense
 * fixtures and streaming interest, so no field implementer or engine world is needed
 * by the suite itself. Adapter-specific setup belongs in that adapter's factory.
 */
using FTerrainBackendFactory = TFunction<TUniquePtr<ITerrainBackend>()>;

TERRAINCORE_API bool RunTerrainBackendConformance(FAutomationTestBase& Test, FTerrainBackendFactory Factory);
TERRAINCORE_API bool RunTerrainPointConformance(FAutomationTestBase& Test, FTerrainBackendFactory Factory);

#endif // WITH_DEV_AUTOMATION_TESTS

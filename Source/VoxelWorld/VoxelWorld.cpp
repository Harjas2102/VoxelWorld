// Copyright VoxelWorld. See Docs/ARCHITECTURE.md.
//
// Primary game module (ARCHITECTURE.md §4.1): gameplay, characters, tools, UI hooks.
// It depends on TerrainCore and on nothing below it.

#include "VoxelWorld.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, VoxelWorld, "VoxelWorld");

<#
.SYNOPSIS
    Launch VoxelWorld standalone, single player, for solo terrain testing (T-101A).

.DESCRIPTION
    PIE is deliberately left configured for the CP-001 three-player replication
    test (PlayNetMode=PIE_ListenServer, PlayNumberOfClients=3), because that is
    what T-101B needs. But Voxel Plugin Free Legacy refuses to use the player
    camera as its LOD invoker in any non-standalone net mode:

        "Voxel World: Can't use camera as invoker in multiplayer!
         You need to add a VoxelInvokerComponent to your character"

    Without an invoker the render octree never subdivides, so terrain shows up as
    one coarse blob and traces into it miss. See Docs/T-101A_FINDINGS.md 2d and
    RISKS.md R-010.

    So: solo terrain work runs standalone, which is Standalone net mode by
    definition and picks up VoxelInvokerAutoCameraComponent automatically. It runs
    as a separate process, so the editor can stay open alongside it.

.EXAMPLE
    .\Tools\Play-Solo.ps1
    .\Tools\Play-Solo.ps1 -Map /Game/Maps/VoxelSandbox
    .\Tools\Play-Solo.ps1 -Fullscreen

.NOTES
    Project: VoxelWorld - Task: T-101A - Added 2026-09-06
#>
[CmdletBinding()]
param(
    [string]$Map        = "/Game/ThirdPerson/Lvl_ThirdPerson",
    [string]$Engine     = "C:\Program Files\Epic Games\UE_5.7",
    [int]$Width         = 1600,
    [int]$Height        = 900,
    [switch]$Fullscreen,
    [switch]$ShowLog
)

$ErrorActionPreference = "Stop"

$project = Join-Path $PSScriptRoot "..\VoxelWorld.uproject" | Resolve-Path
$exe     = Join-Path $Engine "Engine\Binaries\Win64\UnrealEditor.exe"
$logDir  = Join-Path $PSScriptRoot "..\Saved\Logs" | Resolve-Path
$log     = Join-Path $logDir "Standalone_T101A.log"

if (-not (Test-Path $exe)) {
    throw "Unreal Editor not found at '$exe'. Pass -Engine <path to UE_5.7>."
}

# A dedicated log file: the editor holds Saved/Logs/VoxelWorld.log open, and a
# second process would otherwise be shunted into VoxelWorld_2.log, _3.log, ...
# which makes "check the log" ambiguous.
$argList = @(
    "`"$project`""
    $Map
    "-game"
    "-nosplash"
    "-AbsLog=$log"
)
if ($Fullscreen) { $argList += "-fullscreen" }
else             { $argList += @("-windowed", "-resx=$Width", "-resy=$Height") }
if ($ShowLog)    { $argList += "-log" }

Write-Host "Launching standalone: $Map" -ForegroundColor Cyan
$proc = Start-Process -FilePath $exe -ArgumentList $argList -PassThru
Write-Host "  PID $($proc.Id)"
Write-Host "  log $log"
Write-Host ""
Write-Host "Success looks like these lines in the log:" -ForegroundColor Cyan
Write-Host "  World NetMode = Standalone"
Write-Host "  LogVoxel: Voxel Invoker enabled; Name: VoxelInvokerAutoCameraComponent_0"
Write-Host "  LogVoxel: No Voxel Invoker found, using camera as invoker"
Write-Host ""
Write-Host "And NOT this one:" -ForegroundColor Yellow
Write-Host "  Voxel World: Can't use camera as invoker in multiplayer!"

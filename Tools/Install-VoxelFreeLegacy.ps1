<#
.SYNOPSIS
    Installs Voxel Plugin Free Legacy (prebuilt binaries) into Plugins\VoxelFree.

.DESCRIPTION
    The terrain backend is PROVISIONAL (D-010) and deliberately not committed to git:
    Plugins/VoxelFree/ is gitignored because the binaries are ~700 MB of .pdb. This
    script is how the plugin comes back on a fresh clone or a new machine.

    Idempotent. If Plugins\VoxelFree\VoxelFree.uplugin already exists, it reports the
    installed version and exits 0 without downloading anything. Use -Force to reinstall.

    The download URL is NOT hardcoded as the primary source: the script fetches the
    project README from GitHub and extracts whatever binaries link it currently
    advertises for -EngineVersion, falling back to the URL known good at T-112.5 only if
    that fails.
    The archive's internal layout is inspected rather than assumed.

.PARAMETER Force
    Reinstall even if the plugin is already present. The existing folder is moved aside
    to Tools\downloads\VoxelFree.bak-<timestamp> rather than deleted. It goes OUTSIDE
    Plugins\ on purpose: UnrealBuildTool scans Plugins\ recursively, so a backup left
    there makes every module's Build.cs appear twice and the build fails with CS0101
    "already contains a definition for 'Voxel'" before a single file compiles.

.PARAMETER EngineVersion
    Which engine's prebuilt binaries to install, as it appears in the archive name.
    Defaults to 5.8 (D-025, T-112.5). Pass 5.7 to reinstall the pre-upgrade build.

.PARAMETER Url
    Skip README discovery and download this exact zip URL.

.PARAMETER ZipPath
    Skip downloading entirely and install from an already-downloaded zip. Use this when
    the download fails and you fetched the file in a browser instead.

.EXAMPLE
    .\Tools\Install-VoxelFreeLegacy.ps1
.EXAMPLE
    .\Tools\Install-VoxelFreeLegacy.ps1 -ZipPath C:\Users\harja\Downloads\VoxelFree-432.zip

.NOTES
    Project: VoxelWorld  ·  Task: T-101A  ·  Added at CP-002 (2026-09-05)
    Risk R-008 tracks the licensing/version exposure of this dependency.
#>
[CmdletBinding()]
param(
    [switch]$Force,
    [ValidatePattern('^\d+\.\d+$')]
    [string]$EngineVersion = '5.8',
    [string]$Url,
    [string]$ZipPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# --- Paths -------------------------------------------------------------------
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$PluginDir   = Join-Path $ProjectRoot 'Plugins\VoxelFree'
$UpluginPath = Join-Path $PluginDir   'VoxelFree.uplugin'
$DownloadDir = Join-Path $PSScriptRoot 'downloads'

$RepoUrl      = 'https://github.com/VoxelPlugin/VoxelPluginFreeLegacy'
$ReadmeRawUrl = 'https://raw.githubusercontent.com/VoxelPlugin/VoxelPluginFreeLegacy/master/README.md'
# Known good per engine version. Fallback only — README discovery is preferred.
# 5.7: CP-002 (2026-09-05).  5.8: T-112.5 (2026-09-06), the D-025 upgrade target.
$FallbackUrls = @{
    '5.7' = 'https://api.voxelplugin.com/external/7f2800eb480ae2e0289fecb6994aac5e/VoxelFree-432-e9648b302-5.7-Binaries.zip'
    '5.8' = 'https://api.voxelplugin.com/external/7923261944ecf7909870de8b925e359b/VoxelFree-434-159fd19a0-5.8-Binaries.zip'
}
$FallbackUrl  = $FallbackUrls[$EngineVersion]
# Regex-safe form of the version for the README link patterns below.
$VerPattern   = [regex]::Escape($EngineVersion)

function Write-Step { param([string]$m) Write-Host "==> $m" -ForegroundColor Cyan }
function Write-Ok   { param([string]$m) Write-Host "    $m" -ForegroundColor Green }
function Write-Warn { param([string]$m) Write-Host "    $m" -ForegroundColor Yellow }

function Get-InstalledVersion {
    if (-not (Test-Path $UpluginPath)) { return $null }
    try {
        $d = Get-Content $UpluginPath -Raw | ConvertFrom-Json
        return [pscustomobject]@{
            Version       = $d.Version
            VersionName   = $d.VersionName
            EngineVersion = $d.EngineVersion
            FriendlyName  = $d.FriendlyName
        }
    } catch {
        Write-Warn "VoxelFree.uplugin exists but is not valid JSON: $($_.Exception.Message)"
        return $null
    }
}

# --- 1. Already installed? ---------------------------------------------------
Write-Step "Checking for an existing install at $PluginDir"
$installed = Get-InstalledVersion
if ($installed -and -not $Force) {
    Write-Ok "$($installed.FriendlyName) is already installed."
    Write-Ok "Version $($installed.Version) / $($installed.VersionName) / engine $($installed.EngineVersion)"
    $dll = Join-Path $PluginDir 'Binaries\Win64\UnrealEditor-Voxel.dll'
    if (Test-Path $dll) {
        Write-Ok "Prebuilt binaries present (UnrealEditor-Voxel.dll)."
    } else {
        Write-Warn "No prebuilt Win64 binaries found - the plugin will need to compile from source."
    }
    Write-Host ''
    Write-Host 'Nothing to do. Re-run with -Force to reinstall.' -ForegroundColor Green
    exit 0
}
if ($installed -and $Force) {
    # Outside Plugins\ deliberately - see .PARAMETER Force. Tools\downloads is gitignored.
    if (-not (Test-Path $DownloadDir)) { New-Item -ItemType Directory -Path $DownloadDir | Out-Null }
    $backup = Join-Path $DownloadDir "VoxelFree.bak-$(Get-Date -Format 'yyyyMMdd-HHmmss')-engine$($installed.EngineVersion)"
    Write-Warn "-Force given; moving the existing install to $backup"
    Move-Item -LiteralPath $PluginDir -Destination $backup
}

# --- 2. Resolve the download URL --------------------------------------------
if (-not $ZipPath) {
    if (-not $Url) {
        Write-Step "Discovering the current $EngineVersion binaries link from the project README"
        try {
            $readme = (Invoke-WebRequest -Uri $ReadmeRawUrl -UseBasicParsing -TimeoutSec 30).Content
            # Any http(s) link to a .zip whose name mentions the engine version and Binaries.
            $m = [regex]::Matches($readme, "https?://[^\s)`"'<>]*$VerPattern[^\s)`"'<>]*Binaries[^\s)`"'<>]*\.zip")
            if ($m.Count -eq 0) {
                $m = [regex]::Matches($readme, "https?://[^\s)`"'<>]*VoxelFree[^\s)`"'<>]*$VerPattern[^\s)`"'<>]*\.zip")
            }
            if ($m.Count -gt 0) {
                $Url = $m[0].Value
                Write-Ok "README advertises: $Url"
            } else {
                Write-Warn "No $EngineVersion binaries link found in the README."
            }
        } catch {
            Write-Warn "Could not fetch the README: $($_.Exception.Message)"
        }
    }
    if (-not $Url) {
        if (-not $FallbackUrl) {
            throw "No README link found for engine $EngineVersion and no known-good fallback for it. Open $RepoUrl, copy the current $EngineVersion binaries link, and re-run with -Url <link>."
        }
        $Url = $FallbackUrl
        Write-Warn "Falling back to the URL known good for engine ${EngineVersion}:"
        Write-Warn "  $Url"
        Write-Warn "If this 404s, open $RepoUrl and copy the current $EngineVersion binaries link."
    }

    # --- 3. Download ---------------------------------------------------------
    if (-not (Test-Path $DownloadDir)) { New-Item -ItemType Directory -Path $DownloadDir | Out-Null }
    $ZipPath = Join-Path $DownloadDir ([System.IO.Path]::GetFileName(($Url -split '\?')[0]))
    if (-not $ZipPath.EndsWith('.zip')) { $ZipPath = Join-Path $DownloadDir "VoxelFree-$EngineVersion-Binaries.zip" }

    Write-Step "Downloading to $ZipPath"
    Write-Warn "This is a large archive (several hundred MB). Be patient."
    try {
        $pp = $ProgressPreference; $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri $Url -OutFile $ZipPath -UseBasicParsing -TimeoutSec 1800
        $ProgressPreference = $pp
    } catch {
        Write-Host ''
        Write-Host 'DOWNLOAD FAILED.' -ForegroundColor Red
        Write-Host "  1. Open this in a browser: $Url"
        Write-Host "     (or find the current $EngineVersion link at $RepoUrl)"
        Write-Host "  2. Save the zip anywhere, then re-run:"
        Write-Host "     .\Tools\Install-VoxelFreeLegacy.ps1 -ZipPath <path-to-zip>"
        throw
    }
    Write-Ok ("Downloaded {0:N1} MB" -f ((Get-Item $ZipPath).Length / 1MB))
}

if (-not (Test-Path $ZipPath)) { throw "Zip not found: $ZipPath" }

# --- 4. Extract to a staging folder -----------------------------------------
$Staging = Join-Path $DownloadDir "extract-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
Write-Step "Extracting to $Staging"
if (-not (Test-Path $DownloadDir)) { New-Item -ItemType Directory -Path $DownloadDir | Out-Null }
New-Item -ItemType Directory -Path $Staging | Out-Null
Expand-Archive -LiteralPath $ZipPath -DestinationPath $Staging -Force

# --- 5. Locate the real plugin root INSIDE the archive (do not assume) -------
Write-Step "Inspecting the archive layout"
$found = Get-ChildItem -Path $Staging -Filter 'VoxelFree.uplugin' -Recurse -File |
         Sort-Object { $_.FullName.Length } | Select-Object -First 1
if (-not $found) {
    $anyUplugin = Get-ChildItem -Path $Staging -Filter '*.uplugin' -Recurse -File
    Write-Host 'Could not find VoxelFree.uplugin in the archive.' -ForegroundColor Red
    if ($anyUplugin) {
        Write-Host 'The archive does contain these .uplugin files:'
        $anyUplugin | ForEach-Object { Write-Host "  $($_.FullName.Substring($Staging.Length + 1))" }
    }
    throw 'Unexpected archive layout - inspect it manually and use -ZipPath after fixing.'
}
$SourceRoot = $found.Directory.FullName
# The 5.7 archive wraps the plugin in a folder; the 5.8 archive does not, so SourceRoot
# can equal Staging exactly. Report that as the archive root rather than off the end.
$RelSourceRoot = if ($SourceRoot.Length -gt $Staging.Length) {
    $SourceRoot.Substring($Staging.Length + 1)
} else {
    '<archive root>'
}
Write-Ok "Plugin root inside archive: $RelSourceRoot"

# --- 6. Normalize into Plugins\VoxelFree ------------------------------------
Write-Step "Installing into $PluginDir"
$PluginsParent = Split-Path -Parent $PluginDir
if (-not (Test-Path $PluginsParent)) { New-Item -ItemType Directory -Path $PluginsParent | Out-Null }
Move-Item -LiteralPath $SourceRoot -Destination $PluginDir
Remove-Item -LiteralPath $Staging -Recurse -Force -ErrorAction SilentlyContinue

# --- 7. Verify and report ----------------------------------------------------
$installed = Get-InstalledVersion
if (-not $installed) { throw "Install finished but $UpluginPath is missing or unreadable." }

Write-Host ''
Write-Step 'Installed'
Write-Ok "$($installed.FriendlyName) $($installed.Version) / $($installed.VersionName) / engine $($installed.EngineVersion)"
Write-Host ''
Write-Host 'Plugins\VoxelFree tree (2 levels):' -ForegroundColor Cyan
Get-ChildItem -Path $PluginDir -Depth 1 |
    ForEach-Object { '  ' + $_.FullName.Substring($ProjectRoot.Length + 1) }

$dll = Join-Path $PluginDir 'Binaries\Win64\UnrealEditor-Voxel.dll'
Write-Host ''
if (Test-Path $dll) {
    Write-Ok 'Prebuilt Win64 binaries present - no compiler needed to open the editor.'
} else {
    Write-Warn 'No prebuilt Win64 binaries - the editor will ask to build from source (needs T-100 / VS 2022).'
}
Write-Host ''
Write-Host 'Next: open the project and confirm "Mounting Project plugin VoxelFree" in' -ForegroundColor Green
Write-Host 'Saved\Logs\VoxelWorld.log. See Docs\T-101A_RUNBOOK.md.' -ForegroundColor Green

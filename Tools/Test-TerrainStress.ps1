<# T-132: the gate-8 edit stress profile and E-6 (a joiner arriving in a heavily edited region, mid-edit).
   A dedicated server runs 32 synthetic sources at the 96 ops/s design point until -Edits have committed,
   then a fresh client joins while editing continues; the joiner's copy of every edited chunk is verified. #>
[CmdletBinding()]
param([int]$Edits=5000,[int]$Bots=32,[int]$LiveSeconds=20,[int]$RegionMeters=100,[int]$Port=17787,[string[]]$ServerIni=@(),[string[]]$ExtraArgs=@(),
      [string]$Engine='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$project=Join-Path $root 'VoxelWorld.uproject'
$exe=Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$world='StressTest-'+[guid]::NewGuid().ToString('N').Substring(0,12)
$logs=Join-Path $root ('Saved\Logs\'+$world)
New-Item -ItemType Directory -Path $logs | Out-Null
$serverLog=Join-Path $logs 'Server.log'; $clientLog=Join-Path $logs 'Joiner.log'
$procs=[System.Collections.Generic.List[System.Diagnostics.Process]]::new()
$ini='-ini:Engine:[/Script/TerrainCore.TerrainSettings]:'
try {
    $budget=[int]($Edits/80)+600
    $serverArgs=@('"'+$project+'"','/Game/ThirdPerson/Lvl_ThirdPerson','-server','-nullrhi','-unattended','-nosplash','-nosound',
        '-TerrainStress',"-TerrainStressEdits=$Edits","-TerrainStressBots=$Bots","-TerrainStressLive=$LiveSeconds","-TerrainStressRegion=$RegionMeters",
        "-port=$Port","-seconds=$budget",('-abslog="'+$serverLog+'"'),($ini+"WorldStoreName=$world"))
    # Variants for attribution, e.g. -ServerIni bPersistEdits=False or bCheckpointCapture=False.
    foreach ($setting in $ServerIni) { $serverArgs+=($ini+$setting) }
    foreach ($arg in $ExtraArgs) { $serverArgs+=$arg }   # e.g. -trace=cpu,frame for Unreal Insights
    $server=Start-Process -FilePath $exe -ArgumentList $serverArgs -WindowStyle Hidden -PassThru; $procs.Add($server)
    Write-Output "Server PID $($server.Id); logs $logs"
    $deadline=(Get-Date).AddSeconds([int]($Edits/40)+240)
    do {
        if ($server.HasExited) { throw 'Server exited during phase A.' }
        if ((Test-Path $serverLog) -and (Select-String -LiteralPath $serverLog -Pattern 'Stress: phase B' -Quiet)) { break }
        if ((Get-Date) -gt $deadline) { throw 'Phase A did not finish in time.' }
        Start-Sleep -Seconds 1
    } while ($true)
    Write-Output 'Phase A done; the joiner connects now, mid-edit.'
    $clientArgs=@('"'+$project+'"',"127.0.0.1:$Port",'-game','-nullrhi','-unattended','-nosplash','-nosound',"-seconds=$budget",('-abslog="'+$clientLog+'"'))
    $client=Start-Process -FilePath $exe -ArgumentList $clientArgs -WindowStyle Hidden -PassThru; $procs.Add($client)
    $deadline=(Get-Date).AddSeconds(600)
    do {
        $result=Select-String -LiteralPath $serverLog -Pattern '\*\*\*\* Stress: (PASS|FAIL)' | Select-Object -Last 1
        if ($result) { break }
        if ($server.HasExited) { throw 'Server exited before a result.' }
        if ((Get-Date) -gt $deadline) { throw 'No stress result in time.' }
        Start-Sleep -Seconds 1
    } while ($true)
    Start-Sleep -Seconds 3   # let the audit line land
    Write-Output '--- server ---'
    Select-String -LiteralPath $serverLog -Pattern 'Stress: phase|Stress.Summary|Stress.Join|Stress: verifying|LedgerAudit: |\*\*\*\* Stress|Checkpoint published|Retention: ' |
        ForEach-Object { $_.Line.Substring([Math]::Max(0,$_.Line.IndexOf('LogTerrainCore'))) } | Select-Object -Last 14
    $seconds=@(Select-String -LiteralPath $serverLog -Pattern 'Stress.Second phase=(\d) ops=(\d+) tick_max=([\d.]+)ms frame_max=([\d.]+)ms queue=(\d+) unsettled=(\d+) dirty=(\d+)' | ForEach-Object { $_.Matches[0] })
    foreach ($phase in 1,2) {
        $rows=@($seconds | Where-Object { $_.Groups[1].Value -eq "$phase" })
        if ($rows.Count -gt 2) {
            $ops=$rows | ForEach-Object { [int]$_.Groups[2].Value } | Sort-Object
            $tick=$rows | ForEach-Object { [double]$_.Groups[3].Value } | Sort-Object
            $frame=$rows | ForEach-Object { [double]$_.Groups[4].Value } | Sort-Object
            $p=[int][Math]::Floor($rows.Count*0.95)-1; if ($p -lt 0) { $p=0 }
            Write-Output ("Phase {0}: {1} seconds; ops/s median {2}, min {3}; service tick max per second: p95 {4:N1} ms, worst {5:N1} ms; frame max per second: p95 {6:N1} ms, worst {7:N1} ms" -f `
                $phase,$rows.Count,$ops[[int]($ops.Count/2)],$ops[0],$tick[$p],$tick[-1],$frame[$p],$frame[-1])
        }
    }
    Write-Output '--- joiner ---'
    $installs=@(Select-String -LiteralPath $clientLog -Pattern 'installed: (\d+) bytes in ([\d.]+) ms' | ForEach-Object { $_.Matches[0] })
    if ($installs.Count) {
        $ms=$installs | ForEach-Object { [double]$_.Groups[2].Value } | Sort-Object
        $bytes=($installs | ForEach-Object { [int]$_.Groups[1].Value } | Measure-Object -Sum).Sum
        Write-Output ("{0} snapshots installed, {1:N0} bytes; install ms median {2:N1}, p95 {3:N1}, max {4:N1}, total {5:N0} ms" -f `
            $installs.Count,$bytes,$ms[[int]($ms.Count/2)],$ms[[int]($ms.Count*0.95)-1],$ms[-1],($ms | Measure-Object -Sum).Sum)
    }
    $frames=@(Select-String -LiteralPath $clientLog -Pattern 'Stress.ClientFrame max ([\d.]+) ms' | ForEach-Object { [double]$_.Matches[0].Groups[1].Value })
    if ($frames.Count) { Write-Output ("Client longest frame per 5 s window: " + (($frames | ForEach-Object { '{0:N0}' -f $_ }) -join ', ') + ' ms') }
    Write-Output '--- save ---'
    $dir=Join-Path $root "Saved\Worlds\$world"
    $size=0; if (Test-Path $dir) { $size=(Get-ChildItem -LiteralPath $dir -Recurse -File | Measure-Object -Property Length -Sum).Sum }
    $head=[regex]::Match((Get-Content -LiteralPath $serverLog -Raw),'Stress.Summary total: ops=(\d+)').Groups[1].Value
    if ($size -and $head) { Write-Output ("World directory {0:N1} MB after {1} commits ({2:N0} bytes/commit, all-in: containers, journal, ledger)" -f ($size/1MB),$head,($size/[double]$head)) }
    if ($result.Line -notmatch 'Stress: PASS') { throw "Stress FAILED: $($result.Line)" }
    Write-Output "PASS. Evidence: $logs"
} finally {
    foreach ($p in $procs) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }; $p.Dispose() }
}

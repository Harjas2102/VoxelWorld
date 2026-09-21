<# Dedicated server plus three real clients; exact server/client chunk hashes after a shared edit workload. #>
[CmdletBinding()]
param([int]$DurationSeconds=60,[ValidateRange(1,3)][int]$Rounds=1,[int]$Port=17777,[switch]$IncludeObserver,
      [switch]$CheckpointCapture,
      [string]$Engine='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
if ($DurationSeconds -lt 5 -or $DurationSeconds -gt 120) { throw 'Duration must be 5..120 seconds.' }
$taskRoot=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$taskProject=Join-Path $taskRoot 'VoxelWorld.uproject'
$taskExe=Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskLogDir=Join-Path $taskRoot ('Saved\Logs\T101B-MP-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $taskLogDir | Out-Null
$taskProcesses=[System.Collections.Generic.List[System.Diagnostics.Process]]::new()
try {
    $serverLog=Join-Path $taskLogDir 'Server.log'
    $serverArgs=@('"'+$taskProject+'"','/Game/ThirdPerson/Lvl_ThirdPerson','-server','-nullrhi','-unattended','-nosplash','-nosound',
        '-TerrainMPTest',('-TerrainMPRounds='+$Rounds),('-TerrainMPDuration='+$DurationSeconds),('-port='+$Port),('-seconds='+($DurationSeconds*$Rounds+150)),('-abslog="'+$serverLog+'"'))
    # Every run gets its own save. Never write test operations into the player's Default world.
    $serverArgs+=('-ini:Engine:[/Script/TerrainCore.TerrainSettings]:WorldStoreName=MPTest-'+[guid]::NewGuid().ToString('N'))
    if ($CheckpointCapture) {
        $serverArgs+='-ini:Engine:[/Script/TerrainCore.TerrainSettings]:bCheckpointCapture=True'
        $serverArgs+='-ini:Engine:[/Script/TerrainCore.TerrainSettings]:CheckpointOpTrigger=16'
    }
    if ($IncludeObserver) { $serverArgs+='-TerrainMPExpectObserver' }
    $server=Start-Process -FilePath $taskExe -ArgumentList $serverArgs -WindowStyle Hidden -PassThru
    $taskProcesses.Add($server)
    Write-Output "Server PID $($server.Id); logs $taskLogDir"
    $readyDeadline=(Get-Date).AddSeconds(60)
    do {
        if ($server.HasExited) { throw 'Server exited before ready.' }
        if ((Test-Path $serverLog) -and (Select-String -LiteralPath $serverLog -Pattern "Terrain backend .* ready:" -Quiet)) { break }
        if ((Get-Date) -gt $readyDeadline) { throw 'Server startup timeout.' }
        Start-Sleep -Milliseconds 500
    } while ($true)
    $count=3
    if ($IncludeObserver) { $count=4 }
    for ($i=0;$i -lt $count;$i++) {
        $clientLog=Join-Path $taskLogDir "Client$i.log"
        $clientArgs=@('"'+$taskProject+'"',("127.0.0.1:"+$Port),'-game','-nullrhi','-unattended','-nosplash','-nosound',
            ('-seconds='+($DurationSeconds*$Rounds+135)),('-abslog="'+$clientLog+'"'))
        if ($i -eq 3) { $clientArgs+='-TerrainMPObserver' }
        $client=Start-Process -FilePath $taskExe -ArgumentList $clientArgs -WindowStyle Hidden -PassThru
        $taskProcesses.Add($client)
        Write-Output "Client $i PID $($client.Id)"
    }
    $deadline=(Get-Date).AddSeconds($DurationSeconds*$Rounds+140)
    do {
        $results=@(Select-String -LiteralPath $serverLog -Pattern '\*\*\*\* MP.Convergence: (PASS|FAIL)')
        $result=$results | Select-Object -Last 1
        if ($result) {
            foreach ($entry in $results) {
                if ($entry.Line -notmatch 'MP.Convergence: PASS') { throw "Multiplayer hashes failed: $($entry.Line)" }
                if ($entry.Line -notmatch ("clients="+$count+' ')) { throw 'Not all expected clients were verified.' }
            }
            if ($results.Count -ge $Rounds) {
                $results | ForEach-Object { Write-Output $_.Line }
                Write-Output "PASS. Evidence: $taskLogDir"
                break
            }
        }
        if ($server.HasExited) { throw 'Server exited before a result.' }
        if ((Get-Date) -gt $deadline) { throw 'Multiplayer test timed out; inspect logs.' }
        Start-Sleep -Milliseconds 500
    } while ($true)
} finally {
    # These are exclusively process handles created by this invocation; never match unrelated editors.
    foreach ($taskProcess in $taskProcesses) {
        if (-not $taskProcess.HasExited) { Stop-Process -Id $taskProcess.Id -Force -ErrorAction SilentlyContinue }
        $taskProcess.Dispose()
    }
}

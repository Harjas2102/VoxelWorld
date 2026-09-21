"""Measure object retention with three distinct captures and verify restart hashes.

Uses a unique world and preserves it and every log. Does not touch existing saves.
"""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', default=r'C:\Program Files\Epic Games\UE_5.8')
    parser.add_argument('--chunks', type=int, default=256)
    args = parser.parse_args()
    if not 1 <= args.chunks <= 4096:
        parser.error('--chunks must be 1..4096')
    project = Path(__file__).resolve().parents[1]
    executable = Path(args.engine) / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    name = 'RetentionTest-' + uuid.uuid4().hex[:12]
    logs = project / 'Saved/Logs' / name
    logs.mkdir(parents=True)
    save = project / 'Saved/Worlds' / name
    ini = '-ini:Engine:[/Script/TerrainCore.TerrainSettings]:'
    base = [str(executable), str(project / 'VoxelWorld.uproject'),
            '/Game/ThirdPerson/Lvl_ThirdPerson', '-game', '-nullrhi', '-unattended',
            '-nosplash', '-nosound', '-TerrainRetentionExperiment', ini + f'WorldStoreName={name}',
            ini + f'CheckpointDirtyChunkTrigger={args.chunks}',
            ini + 'CheckpointOpTrigger=1000000']

    def run(label, commands, seconds=3, extra=()):
        log = logs / (label + '.log')
        command = base + [f'-ExecCmds={commands}', f'-seconds={seconds}', f'-abslog={log}'] + list(extra)
        print(f'{label}: running; {log}', flush=True)
        result = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
                                timeout=seconds + 180, creationflags=subprocess.CREATE_NO_WINDOW)
        text = log.read_text(encoding='utf-8', errors='replace')
        if result.returncode or 'Fatal error:' in text or 'TERRAIN ACCESS IS CLOSED' in text:
            raise RuntimeError(f'{label} failed: {log}')
        return text

    def hashes(text):
        entries = re.findall(r'Terrain.StressCapture.Hash (\d+)=([0-9a-f]+)', text)
        if len(entries) != args.chunks or any(int(v, 16) == 0 for _, v in entries):
            raise RuntimeError('Missing or zero chunk hashes')
        return dict(entries)

    previous = None
    generations = []
    for index in range(3):
        text = run(f'Capture{index+1}', 'Terrain.StressCapture',
                   seconds=max(15, int(args.chunks / 3) + 25),
                   extra=['-TerrainStressRemove'] if index == 1 else [])
        match = re.search(r'issued=(\d+) applied=(\d+) rejected=(\d+) .*?voxels=(\d+)', text)
        if (not match or tuple(map(int, match.groups()[:3])) != (args.chunks, args.chunks, 0)
                or int(match.group(4)) == 0):
            raise RuntimeError('Workload did not actually modify terrain')
        cuts = re.findall(r'Checkpoint published at G=(\d+)', text)
        expected = (index + 1) * args.chunks
        if not cuts or int(cuts[-1]) != expected:
            raise RuntimeError(f'Missing checkpoint at G={expected}')
        current = hashes(run(f'Hashes{index+1}', 'Terrain.StressCaptureHashes'))
        if previous is not None and current == previous:
            raise RuntimeError('Successive workload generations have identical terrain')
        previous = current
        generations.append(current)
        print(f'Capture {index+1}: G={expected}, {match.group(4)} voxels changed, '
              f'{len(current)} restored hashes verified', flush=True)

    def file_hashes():
        return {str(p.relative_to(save)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in save.rglob('*') if p.is_file()}

    before_files = file_hashes()
    before_bytes = sum(p.stat().st_size for p in save.rglob('*') if p.is_file())
    reclaim = run('Reclaim', 'Terrain.Reclaim,Terrain.StressCaptureHashes')
    if hashes(reclaim) != generations[-1]:
        raise RuntimeError('Reclamation changed current terrain')
    report = re.search(r'\*\*\*\* Terrain.Reclaim: (.+?) \*\*\*\*', reclaim)
    if not report:
        raise RuntimeError('Reclamation did not complete')
    after_bytes = sum(p.stat().st_size for p in save.rglob('*') if p.is_file())
    if after_bytes >= before_bytes:
        raise RuntimeError('Reclamation did not reduce the save')
    after_files = file_hashes()
    for path, digest in before_files.items():
        if not path.startswith(('objects', 'packs', 'containers')) and after_files.get(path) != digest:
            raise RuntimeError(f'Reclamation changed protected file {path}')
    # P-005: an open world creates and removes no names. Reclamation truncates containers; it
    # must leave exactly the same set of files behind, on a real disk.
    if set(after_files) != set(before_files):
        raise RuntimeError('Reclamation created or removed a file name: '
                           f'+{sorted(set(after_files) - set(before_files))} '
                           f'-{sorted(set(before_files) - set(after_files))}')
    if hashes(run('Restart', 'Terrain.StressCaptureHashes')) != generations[-1]:
        raise RuntimeError('Restart after reclamation changed terrain')
    second = run('SecondSweep', 'Terrain.Reclaim')
    if '0 bytes reclaimed' not in second or file_hashes() != after_files:
        raise RuntimeError('A second sweep was not idempotent')
    print(report.group(1), flush=True)
    print(f'PASS: {before_bytes} -> {after_bytes} bytes; all {args.chunks} terrain hashes '
          f'survive reclamation and restart. Logs: {logs}\nSave preserved: {save}', flush=True)


if __name__ == '__main__':
    main()

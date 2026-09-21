"""Real production-backend restart checks. Uses a unique save; never removes saves."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', default=r'C:\Program Files\Epic Games\UE_5.8')
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    executable = Path(args.engine) / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    name = 'CheckpointTest-' + uuid.uuid4().hex[:12]
    logs = project / 'Saved/Logs' / name
    logs.mkdir(parents=True)
    ini = '-ini:Engine:[/Script/TerrainCore.TerrainSettings]:'
    previous = None
    for index, capture in enumerate((False, False, True, True), 1):
        log = logs / f'Run{index}.log'
        command = [str(executable), str(project / 'VoxelWorld.uproject'),
                   '/Game/ThirdPerson/Lvl_ThirdPerson', '-game', '-nullrhi',
                   '-unattended', '-nosplash', '-nosound', '-seconds=12',
                   '-ExecCmds=Terrain.SelfTest', f'-abslog={log}',
                   ini + f'WorldStoreName={name}']
        # Omit the capture switch in runs 1/2 to test the actual default.
        if capture:
            command += [ini + 'bCheckpointCapture=True',
                        ini + f'CheckpointDirtyChunkTrigger={1 if index == 3 else 4096}',
                        ini + 'CheckpointOpTrigger=2']
        result = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
                                timeout=180, creationflags=subprocess.CREATE_NO_WINDOW)
        text = log.read_text(encoding='utf-8', errors='replace')
        if result.returncode or '**** Terrain.SelfTest: PASS ****' not in text:
            raise RuntimeError(f'Run {index} failed: {log}')
        before, after = {}, {}
        for phase, key, value in re.findall(r'Terrain.SelfTest.Hash (before|after) (\([^)]+\))=([0-9a-f]+)', text):
            (before if phase == 'before' else after)[key] = value
        if len(before) != 8 or len(after) != 8 or any(int(v, 16) == 0 for v in before.values()):
            raise RuntimeError(f'Incomplete chunk hash evidence: {log}')
        if previous is not None and before != previous:
            raise RuntimeError(f'Restart changed terrain hashes: {log}')
        previous = after
        published = 'Checkpoint published at G=' in text
        if published != capture:
            raise RuntimeError(f'Unexpected capture state: {log}')
        if index == 4 and not re.search(r'restored: 8 chunks from the checkpoint at G=6, then 0 edits', text):
            raise RuntimeError(f'Checkpoint restore did not replace operation replay: {log}')
        print(f'Run {index}: PASS capture={capture}, 8 hashes verified', flush=True)
        for line in text.splitlines():
            if 'Checkpoint published at' in line or "restored: " in line:
                print(line, flush=True)
    save = project / 'Saved/Worlds' / name
    def save_hashes():
        return {str(p.relative_to(save)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in save.rglob('*') if p.is_file()}
    saved = save_hashes()
    mismatch_log = logs / 'WrongBase.log'
    mismatch = [arg for arg in command if not arg.startswith(('-ExecCmds=', '-abslog='))]
    mismatch += [ini + 'Seed=12345', f'-abslog={mismatch_log}']
    result = subprocess.run(mismatch, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
                            timeout=180, creationflags=subprocess.CREATE_NO_WINDOW)
    text = mismatch_log.read_text(encoding='utf-8', errors='replace')
    if (result.returncode or 'different world shape' not in text
            or 'TERRAIN ACCESS IS CLOSED' not in text
            or re.search(r"Terrain backend .* ready:", text) or save_hashes() != saved):
        raise RuntimeError(f'Wrong-base boot failed to close safely: {mismatch_log}')
    print('Wrong-base boot: PASS access closed, every save file unchanged', flush=True)
    print(f'PASS. Logs: {logs}\nSave preserved: {save}')


if __name__ == '__main__':
    main()

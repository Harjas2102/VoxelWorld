"""Exclusive-writer lease across real processes (P-003 §5, T-128). Uses a unique save; never removes saves.

1. Server A opens a new world and stays up, idle.
2. Server B is pointed at the same world while A runs, and tries to dig (Terrain.SelfTest).
   B must refuse with StoreBusy, close terrain access, never open the store, and change no
   byte of the save.
3. A is killed hard -- TerminateProcess, the same as a crash. An OS lock dies with its process,
   so no stale lease may survive.
4. Server C opens the same world and must dig and pass Terrain.SelfTest.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import time
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', default=r'C:\Program Files\Epic Games\UE_5.8')
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    executable = Path(args.engine) / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    name = 'LeaseTest-' + uuid.uuid4().hex[:12]
    logs = project / 'Saved/Logs' / name
    logs.mkdir(parents=True)
    save = project / 'Saved/Worlds' / name
    ini = '-ini:Engine:[/Script/TerrainCore.TerrainSettings]:'

    def command(log, seconds, extra=()):
        return [str(executable), str(project / 'VoxelWorld.uproject'),
                '/Game/ThirdPerson/Lvl_ThirdPerson', '-game', '-nullrhi',
                '-unattended', '-nosplash', '-nosound', f'-seconds={seconds}',
                f'-abslog={log}', ini + f'WorldStoreName={name}', *extra]

    def read(log):
        return log.read_text(encoding='utf-8', errors='replace') if log.exists() else ''

    def save_hashes():
        # writer.lock is held by A and its byte 0 is locked, so it is listed, not read.
        return {str(p.relative_to(save)): (None if p.name == 'writer.lock'
                                           else hashlib.sha256(p.read_bytes()).hexdigest())
                for p in save.rglob('*') if p.is_file()}

    # --- 1. server A holds the world ---------------------------------------------------------
    log_a = logs / 'A-holder.log'
    holder = subprocess.Popen(command(log_a, 600), stdout=subprocess.DEVNULL,
                              stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        deadline = time.time() + 180
        while 'World store opened' not in read(log_a):
            if holder.poll() is not None or time.time() > deadline:
                raise RuntimeError(f'Server A never opened its world: {log_a}')
            time.sleep(1)
        # Let A's boot finish its own writes (none are expected with no edits) before sampling.
        time.sleep(5)
        before = save_hashes()
        if 'writer.lock' not in before:
            raise RuntimeError(f'No lock file in the save: {save}')
        print(f'A: world opened and held ({len(before)} files)', flush=True)

        # --- 2. server B must be refused -------------------------------------------------------
        log_b = logs / 'B-intruder.log'
        result = subprocess.run(command(log_b, 12, ['-ExecCmds=Terrain.SelfTest']),
                                stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, timeout=240,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        text = read(log_b)
        checks = {
            'refused with StoreBusy': 'already open for writing by another server (StoreBusy)' in text,
            'terrain access closed': 'TERRAIN ACCESS IS CLOSED' in text,
            'never opened the store': 'World store opened' not in text,
            'never created a world': 'Creating a new terrain world' not in text,
            'its dig was refused': 'Remove r=200cm -> applied=0 reason=ShuttingDown' in text,
            'did not pass SelfTest': '**** Terrain.SelfTest: PASS ****' not in text,
            'save unchanged': save_hashes() == before,
            'A still running': holder.poll() is None,
        }
        failed = [k for k, ok in checks.items() if not ok]
        if failed:
            raise RuntimeError(f'Server B: {failed} (exit {result.returncode}): {log_b}')
        print('B: PASS ' + ', '.join(checks), flush=True)
        for line in text.splitlines():
            if 'StoreBusy' in line:
                print('   ' + line.strip(), flush=True)

        # --- 3. kill A the way a crash would ----------------------------------------------------
        holder.kill()   # TerminateProcess on Windows: no destructors, no clean close
        holder.wait(timeout=60)
        print('A: killed hard', flush=True)
    finally:
        if holder.poll() is None:
            holder.kill()

    # --- 4. server C inherits the world ---------------------------------------------------------
    log_c = logs / 'C-successor.log'
    result = subprocess.run(command(log_c, 12, ['-ExecCmds=Terrain.SelfTest']),
                            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, timeout=240,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    text = read(log_c)
    if (result.returncode or 'StoreBusy' in text or 'World store opened' not in text
            or '**** Terrain.SelfTest: PASS ****' not in text):
        raise RuntimeError(f'Server C could not take over after a crash: {log_c}')
    print('C: PASS no stale lease after a hard kill; opened the world and SelfTest passed', flush=True)
    print(f'PASS. Logs: {logs}\nSave preserved: {save}')


if __name__ == '__main__':
    main()

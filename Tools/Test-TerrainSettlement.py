"""Settlement survives hard kills (P-003 §2/§3, P-010, DEF-1). Uses a unique save; never removes saves.

Each iteration: a server digs continuously (Terrain.DigStress, every dig credited to the server's
owner) and is killed hard at a random moment. Alternate iterations slow the ledger worker
(-TerrainSettleDelay) so kills land between "journaled" and "settled". Then a clean boot runs
Terrain.LedgerAudit, which recomputes every balance from the journal alone: it must PASS (no
credit missing, none paid twice, W = H), and the boot must report how many records it settled.

Then two refusals, each on a copy of the world:
  - the ledger deleted: boot must refuse, never open a blank one over paid history;
  - the ledger replaced by an older copy (W < G): boot must refuse it as an old database.
"""
import argparse
import hashlib
from pathlib import Path
import random
import re
import shutil
import subprocess
import time
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', default=r'C:\Program Files\Epic Games\UE_5.8')
    parser.add_argument('--iterations', type=int, default=6)
    parser.add_argument('--seed', type=int, default=None)
    args = parser.parse_args()
    rng = random.Random(args.seed)
    project = Path(__file__).resolve().parents[1]
    exe = Path(args.engine) / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    name = 'SettleTest-' + uuid.uuid4().hex[:12]
    logs = project / 'Saved/Logs' / name
    logs.mkdir(parents=True)
    worlds = project / 'Saved/Worlds'
    ini = '-ini:Engine:[/Script/TerrainCore.TerrainSettings]:'

    def command(world, log, seconds, extra=()):
        return [str(exe), str(project / 'VoxelWorld.uproject'), '/Game/ThirdPerson/Lvl_ThirdPerson',
                '-game', '-nullrhi', '-unattended', '-nosplash', '-nosound', f'-seconds={seconds}',
                f'-abslog={log}', ini + f'WorldStoreName={world}', ini + 'CheckpointOpTrigger=48',
                ini + 'CheckpointOpsPerDirtyChunk=0', *extra]   # cuts every 48 edits, as the refusal cases expect (P-012)

    def read(log):
        return log.read_text(encoding='utf-8', errors='replace') if log.exists() else ''

    def run(world, log, seconds, extra=()):
        subprocess.run(command(world, log, seconds, extra), stdout=subprocess.DEVNULL,
                       stderr=subprocess.STDOUT, timeout=300, creationflags=subprocess.CREATE_NO_WINDOW)
        return read(log)

    def hashes(folder):
        return {str(p.relative_to(folder)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in folder.rglob('*') if p.is_file() and p.name != 'writer.lock'}

    previous_total = -1.0
    settled_in_gap = 0
    old_copy = logs / 'ledger-old.db'
    for i in range(1, args.iterations + 1):
        delay = 0.3 if i % 2 == 0 else 0.0
        dig_log = logs / f'{i:02d}-dig.log'
        extra = ['-ExecCmds=Terrain.DigStress']
        if delay:
            extra.append(f'-TerrainSettleDelay={delay}')
        server = subprocess.Popen(command(name, dig_log, 120, extra), stdout=subprocess.DEVNULL,
                                  stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            deadline = time.time() + 180
            while 'Terrain.DigStress: started' not in read(dig_log):
                if server.poll() is not None or time.time() > deadline:
                    raise RuntimeError(f'Iteration {i}: the digger never started: {dig_log}')
                time.sleep(0.5)
            dig_for = rng.uniform(3.0, 7.0)
            time.sleep(dig_for)
        finally:
            server.kill()          # TerminateProcess: no clean shutdown, no final settle, no checkpoint
            server.wait(timeout=60)

        audit_log = logs / f'{i:02d}-audit.log'
        text = run(name, audit_log, 14, ['-ExecCmds=Terrain.LedgerAudit'])
        boot = re.search(r'Settlement: (\d+) records settled at boot \(W (\d+) -> (\d+), G=(\d+), H=(\d+)\)', text)
        audit = re.search(r'\*\*\*\* Terrain\.LedgerAudit: (PASS|FAIL) H=(\d+) W=(\d+) settlements=(-?\d+) paid records=(\d+) balances=(\d+) total=([\d.]+) L', text)
        if not boot or not audit:
            raise RuntimeError(f'Iteration {i}: no boot settlement line or no audit: {audit_log}')
        n, w0, w1, g, h = map(int, boot.groups())
        total = float(audit.group(7))
        if audit.group(1) != 'PASS':
            raise RuntimeError(f'Iteration {i}: LEDGER AUDIT FAILED: {audit_log}')
        if total < previous_total:
            raise RuntimeError(f'Iteration {i}: balances went DOWN ({previous_total} -> {total} L): {audit_log}')
        previous_total = total
        if delay:
            settled_in_gap += n
        print(f'Iteration {i}: killed after {dig_for:.1f} s (settle delay {delay}); boot settled {n} records '
              f'(W {w0} -> {w1}, G={g}, H={h}); audit PASS, {audit.group(5)} paid records, {total:.1f} L', flush=True)
        if i == 1:
            shutil.copy2(worlds / name / 'ledger.db', old_copy)   # after a clean close: no WAL pending
            old_w = w1

    if settled_in_gap == 0:
        raise RuntimeError('No kill landed between journal and ledger: the recovery path was never exercised.')
    print(f'Kills between "journaled" and "settled": the boot pass recovered {settled_in_gap} records.', flush=True)

    # --- refusal 1: the ledger is gone -------------------------------------------------------
    lost = f'{name}-lost'
    shutil.copytree(worlds / name, worlds / lost)
    for leftover in (worlds / lost).glob('ledger.db*'):
        leftover.unlink()
    before = hashes(worlds / lost)
    text = run(lost, logs / 'refuse-missing.log', 12)
    if ('is missing, but this world' not in text or 'TERRAIN ACCESS' not in text
            or 'World store opened' not in text or hashes(worlds / lost) != before):
        raise RuntimeError(f'A missing ledger was not refused safely: {logs / "refuse-missing.log"}')
    print('Refusal: a deleted ledger is refused, nothing written, every file unchanged', flush=True)

    # --- refusal 2: an old copy of the ledger (W < G) ----------------------------------------
    stale = f'{name}-stale'
    shutil.copytree(worlds / name, worlds / stale)
    for leftover in (worlds / stale).glob('ledger.db*'):
        leftover.unlink()
    shutil.copy2(old_copy, worlds / stale / 'ledger.db')
    before = hashes(worlds / stale)
    text = run(stale, logs / 'refuse-stale.log', 12)
    m = re.search(r'ledger watermark W=(\d+) is outside \[G=(\d+), H=(\d+)\]', text)
    if not m or hashes(worlds / stale) != before:
        raise RuntimeError(f'An old ledger copy was not refused safely: {logs / "refuse-stale.log"}')
    print(f'Refusal: an old ledger (W={m.group(1)} < G={m.group(2)}) is refused, every file unchanged', flush=True)
    print(f'PASS. Logs: {logs}\nSaves preserved: {worlds / name}, {worlds / lost}, {worlds / stale}')


if __name__ == '__main__':
    main()

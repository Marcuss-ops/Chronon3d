#!/usr/bin/env python3
"""Software RGBA golden, worker-count and direct-seek particle regression."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CASES = {
    'empty': '2aae7dc846aaf25f1cadf55f1666862046c6db9d65d84bdc07fa039dac405606',
    'full_budget': '8348ebe6cbec0e9fe5d92c5461017ed5c5fa1e4a00085fb1600bb2247d7ba168',
    'clipped': 'bd256e29d8e39c84cfe5d288545a97667f3d5251b0d887e5efae62f1cc4a07b0',
    'bokeh_depth': 'db01b4b0ae8f2bcbc545a18fb17689b48129257606bd8f2040a019fb8bf45f5e',
}
FRAME_BYTES = 64 * 64 * 4

def run(cli, plan, out, workers, start, end):
    env = os.environ.copy()
    env['CHRONON3D_CPU_RENDER_THREADS'] = str(workers)
    command = [str(cli), 'render-plan', '--input', str(plan), '--output', str(out),
               '--backend', 'software', '--video-sink', 'raw', '--start-frame',
               str(start), '--end-frame', str(end)]
    subprocess.run(command, cwd=ROOT, env=env, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    return out.read_bytes()

def main():
    cli = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli'
    with tempfile.TemporaryDirectory(prefix='chronon-particle-gate-') as temp:
        directory = Path(temp)
        for name, golden in CASES.items():
            plan = ROOT / 'golden/particle_environment_gate' / (name + '.json')
            serial = run(cli, plan, directory / (name + '-one.raw'), 1, 0, 2)
            parallel = run(cli, plan, directory / (name + '-four.raw'), 4, 0, 2)
            seek = run(cli, plan, directory / (name + '-seek.raw'), 4, 2, 2)
            assert len(serial) == 3 * FRAME_BYTES and len(seek) == FRAME_BYTES, name + ': unexpected frame size'
            assert hashlib.sha256(serial).hexdigest() == golden, name + ': golden mismatch'
            assert serial == parallel, name + ': worker-count mismatch'
            assert serial[-FRAME_BYTES:] == seek, name + ': direct seek mismatch'
            print(name + ': PASS')
    return 0

if __name__ == '__main__':
    sys.exit(main())

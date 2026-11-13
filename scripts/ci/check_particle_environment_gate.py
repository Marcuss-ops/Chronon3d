#!/usr/bin/env python3
"""Software RGBA golden, worker-count, direct-seek and blend particle gate."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CASES = {
    'empty': '2aae7dc846aaf25f1cadf55f1666862046c6db9d65d84bdc07fa039dac405606',
    'full_budget': 'bdc7aea31c303e4a4d7a5ae1999149fd74000f90b849a72473fbebe790392cf1',
    'clipped': '64853f2e4f1b76bd44a55c1050999422d1f96522a6c62e36715bf08c0946b9f1',
    'bokeh_depth': '55a7524de60a6f71acbc673cdcc4314eb871e8e1a54e61b349295cd14f6096c3',
    'animated_color': '38f138cad3f9aa9b697a859fcbf0a293f621d5b89778ab7cf8a1c42a6fc0a359',
    'blend_normal': 'bd07a85fcba759420e01d9c890121b6bbfe3b8879d562976830f2507e82c81b2',
    'blend_screen': 'fd1889f36f338cd50905c793f86e4d8b59d7538e815b44eac0d127e1f1d0cd15',
}
FRAME_BYTES = 64 * 64 * 4

def run(cli, plan, out, workers, start, end, backend='software'):
    env = os.environ.copy()
    env['CHRONON3D_CPU_RENDER_THREADS'] = str(workers)
    command = [str(cli), 'render-plan', '--input', str(plan), '--output', str(out),
               '--backend', backend, '--video-sink', 'raw', '--start-frame',
               str(start), '--end-frame', str(end)]
    if backend == 'vulkan':
        command += ['--gpu-hot-path-mode', 'require_gpu_native']
    subprocess.run(command, cwd=ROOT, env=env, check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    return out.read_bytes()

def main():
    args = [arg for arg in sys.argv[1:] if arg != '--vulkan']
    cli = Path(args[0]).resolve() if args else ROOT / 'build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli'
    check_vulkan = '--vulkan' in sys.argv[1:]
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
            if name != 'empty':
                assert serial[:FRAME_BYTES] != serial[-FRAME_BYTES:], name + ': motion is frozen'
            if check_vulkan:
                gpu = run(cli, plan, directory / (name + '-vulkan.raw'), 1, 0, 2, 'vulkan')
                gpu_seek = run(cli, plan, directory / (name + '-vulkan-seek.raw'), 4, 2, 2, 'vulkan')
                assert gpu == serial, name + ': software/Vulkan byte parity mismatch'
                assert gpu[-FRAME_BYTES:] == gpu_seek, name + ': Vulkan direct seek mismatch'
            print(name + ': PASS')
    return 0

if __name__ == '__main__':
    sys.exit(main())

#!/usr/bin/env python3
"""Compare an isolated static bokeh particle before encoding on CPU and Vulkan."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CLI = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli'
PLAN = ROOT / 'golden/particle_environment_gate/single_bokeh.json'
with tempfile.TemporaryDirectory(prefix='chronon-bokeh-parity-') as temp:
    frames = []
    for backend in ('software', 'vulkan'):
        output = Path(temp) / (backend + '.raw')
        command = [str(CLI), 'render-plan', '--input', str(PLAN), '--output', str(output),
                   '--backend', backend, '--video-sink', 'raw', '--start-frame', '0', '--end-frame', '0']
        if backend == 'vulkan':
            command += ['--gpu-hot-path-mode', 'require_gpu_native']
        subprocess.run(command, cwd=ROOT, env=os.environ.copy(), check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        frames.append(output.read_bytes())
    if any(len(frame) != 64 * 64 * 4 for frame in frames):
        raise SystemExit('unexpected raw-frame size')
    if not any(frames[0][3::4]):
        raise SystemExit('particle is not visible')
    difference = max(abs(a - b) for a, b in zip(*frames))
    if difference > 1:
        raise SystemExit(f'CPU/Vulkan differ by up to {difference}/255')
    print(f'static bokeh CPU/Vulkan: PASS (maximum {difference}/255)')

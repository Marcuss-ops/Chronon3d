#!/usr/bin/env python3
"""Edge particle parity: rotated 3D exact, scaled/additive within 1/255."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FRAME_BYTES = 64 * 64 * 4
CASES_DIR = ROOT / 'golden/particle_environment_gate'


def variants():
    base = json.loads((CASES_DIR / 'animated_color.json').read_text())
    scaled = copy.deepcopy(base)
    scaled['layers'][0]['scale'] = [0.7, 1.3]
    rotated = copy.deepcopy(scaled)
    rotated['layers'][0]['rotation'] = [0, 0, 27.0]
    rotated['layers'][0]['enable_3d'] = True
    blend = json.loads((CASES_DIR / 'blend_normal.json').read_text())
    blend['layers'][1]['particle_emitter']['blend_mode'] = 'add'
    return {'scaled': scaled, 'rotated_3d': rotated, 'blend_add': blend}


def run(cli, plan, out, backend):
    command = [
        str(cli), 'render-plan', '--input', str(plan), '--output', str(out),
        '--backend', backend, '--video-sink', 'raw', '--start-frame', '0',
        '--end-frame', '2',
    ]
    if backend == 'vulkan':
        command += ['--gpu-hot-path-mode', 'require_gpu_native']
    subprocess.run(command, cwd=ROOT, env=os.environ.copy(), check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    return out.read_bytes()


def main():
    cli = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else (
        ROOT / 'build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli')
    with tempfile.TemporaryDirectory(prefix='chronon-particle-edges-') as temp:
        directory = Path(temp)
        for name, plan in variants().items():
            plan_path = directory / (name + '.json')
            plan_path.write_text(json.dumps(plan))
            cpu = run(cli, plan_path, directory / (name + '-cpu.raw'), 'software')
            gpu = run(cli, plan_path, directory / (name + '-gpu.raw'), 'vulkan')
            assert len(cpu) == 3 * FRAME_BYTES, name + ': unexpected frame size'
            assert any(cpu[3::4]), name + ': particle layer is not visible'
            if name == 'rotated_3d':
                assert cpu == gpu, name + ': expected byte identity'
                print(name + ': PASS (byte-identical)')
            else:
                delta = max(abs(a - b) for a, b in zip(cpu, gpu))
                assert delta <= 1, name + ': differs by up to %d/255' % delta
                print(name + ': PASS (maximum %d/255)' % delta)
    return 0


if __name__ == '__main__':
    sys.exit(main())

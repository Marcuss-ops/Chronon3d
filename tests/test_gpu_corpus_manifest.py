#!/usr/bin/env python3
"""Lock the versioned GPU baseline corpus and acceptance contract."""

import json
import os
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "docs/benchmarks/gpu-corpus-v1.json"
EXPECTED_IDS = [
    *(f"B{i:02d}" for i in range(12)),
    *(f"GPU_TEXT_{i:02d}" for i in range(1, 6)),
]
RUNNER = ROOT / "tools/benchmark_gpu_baseline.sh"
EXPECTED_ACCEPTANCE = {
    "minimum_wall_attribution_percent": 95.0,
    "full_file_reread_count": 0,
    "require_direct_surface_handoff": True,
    "require_execution_downgrade_counter": True,
    "require_zero_software_encode_frames": True,
    "require_zero_video_pipe_fallback_frames": True,
    "require_encoded_frame_count_match": True,
}


def test_manifest_is_complete_and_unique() -> None:
    data = json.loads(MANIFEST.read_text())
    assert data["id"] == "chronon3d-gpu-corpus-v1"
    assert data["version"] == "1.2"
    assert data["acceptance"] == EXPECTED_ACCEPTANCE
    defaults = data["defaults"]
    assert defaults == {
        "frames": 120,
        "fps": 30,
        "warmup_framebuffers": 10,
        "backend": "vulkan",
        "hardware": "nvenc",
        "encoder_backend": "native",
        "gpu_hot_path_mode": "require_gpu_native",
        "fragmented_mp4": True,
    }

    cases = data["cases"]
    assert len(cases) == 17
    ids = [case["id"] for case in cases]
    assert ids == EXPECTED_IDS
    assert len(ids) == len(set(ids))
    assert all(case["scene"] and case["category"] for case in cases)
    assert all(case.get("frames", defaults["frames"]) > 0 for case in cases)
    assert all(1 <= case.get("fps", defaults["fps"]) <= 240 for case in cases)
    assert cases[9]["frames"] == 18000
    assert all(case.get("fragmented_mp4", defaults["fragmented_mp4"]) is True for case in cases)


def test_runner_declares_fail_closed_certification_gates() -> None:
    runner = RUNNER.read_text()
    required_contracts = (
        'manifest.get("version") != "1.2"',
        'len(manifest_cases) != 17',
        'composition_not_registered',
        '"wall-time attribution missing or non-finite accounted_percent"',
        '"job.full_file_reread_count missing or invalid"',
        '"native GPU surface contract missing or empty"',
        '"native NVENC frames mismatch',
        '"video pipe fallback counter missing or invalid"',
        '"GPU surface copies must be 0',
        '"CPU pixel readback bytes must be 0',
        '"encoder staging-copy bytes must be 0',
        '"encoded frame count mismatch',
        '"certification_eligible"',
        '"execution_downgrade_count missing or invalid"',
    )
    assert all(contract in runner for contract in required_contracts)
    assert '"binary_sha256"' in runner
    assert '"manifest_sha256"' in runner
    assert '"content_pack_id"' in runner


def test_runner_closes_sidecar_contract_fail_closed() -> None:
    fake_bin = ROOT / ".tmp/gpu-corpus-test-bin"
    fake_bin.mkdir(parents=True, exist_ok=True)
    fake_cli = fake_bin / "fake-chronon3d-cli"
    fake_cli.write_text(
        "#!/usr/bin/env python3\n"
        "import json, os, pathlib, sys\n"
        "if len(sys.argv) > 1 and sys.argv[1] == 'list':\n"
        " print('ID SIZE FPS FRAMES CATEGORY')\n"
        " print('--------------------------------')\n"
        " raise SystemExit(0)\n"
        "if len(sys.argv) < 2 or sys.argv[1] != 'render': raise SystemExit(2)\n"
        "scene = sys.argv[2]\n"
        "frames_arg = sys.argv[sys.argv.index('--frames') + 1]\n"
        "frame_count = int(frames_arg.split('-')[1]) + 1\n"
        "output = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "output.parent.mkdir(parents=True, exist_ok=True)\n"
        "output.write_text(str(frame_count))\n"
        "scenario = os.environ.get('GPU_CORPUS_TEST_SCENARIO', 'pass')\n"
        "sidecar = {\n"
        " 'frames_total': frame_count, 'frame_times_ms': [{} for _ in range(frame_count)],\n"
        " 'exclusive_wall_timeline': {'accounted_percent': 96.0},\n"
        " 'startup_breakdown': {'total_startup_ms': 1.0},\n"
        " 'job': {'prepare_ms': 1.0, 'encoder_finalize_ms': 1.0,\n"
        "         'output_finalize_ms': 1.0, 'full_file_reread_count': 0,\n"
        "         'execution_path': 'full_graph_native', 'execution_downgrade_count': 1,\n"
        "         'surface_handoff_path': 'direct',\n"
        "         'gpu': {'effective_backend': 'vulkan',\n"
        "                 'gpu_native_surface_frames': frame_count,\n"
        "                 'gpu_native_encode_frames': frame_count,\n"
        "                 'software_encode_frames': 0,\n"
        "                 'video_pipe_fallback_frames': 0,\n"
        "                 'gpu_surface_copy_frames': 0,\n"
        "                 'cpu_pixel_readback_bytes': 0,\n"
        "                 'encoder_staging_copy_bytes': 0}},\n"
        " 'summary': {'warmup_avg_ms': None, 'warmup_frames': 0,\n"
        "             'steady_avg_ms': 1.0, 'windows': {\n"
        "               'prewarm': {'frame_count': 0},\n"
        "               'steady_state': {'frame_count': frame_count}}}}\n"
        "if scenario == 'window_frame_mismatch': sidecar['summary']['windows']['prewarm']['frame_count'] = 1\n"
        "if scenario == 'missing_accounting': sidecar['exclusive_wall_timeline'].pop('accounted_percent')\n"
        "if scenario == 'low_accounting': sidecar['exclusive_wall_timeline']['accounted_percent'] = 94.9\n"
        "if scenario == 'missing_reread': sidecar['job'].pop('full_file_reread_count')\n"
        "if scenario == 'reread': sidecar['job']['full_file_reread_count'] = 1\n"
        "if scenario == 'surface_mismatch': sidecar['job']['gpu']['gpu_native_surface_frames'] -= 1\n"
        "if scenario == 'missing_downgrade': sidecar['job'].pop('execution_downgrade_count')\n"
        "if scenario == 'downgrade': sidecar['job']['execution_downgrade_count'] = -1\n"
        "if scenario == 'handoff_fallback': sidecar['job']['surface_handoff_path'] = 'copy'\n"
        "if scenario == 'missing_encode': sidecar['job']['gpu'].pop('gpu_native_encode_frames')\n"
        "if scenario == 'encode_mismatch': sidecar['job']['gpu']['gpu_native_encode_frames'] -= 1\n"
        "if scenario == 'readback': sidecar['job']['gpu']['cpu_pixel_readback_bytes'] = 4\n"
        "if scenario == 'staging_copy': sidecar['job']['gpu']['encoder_staging_copy_bytes'] = 4\n"
        "if scenario == 'frame_count_mismatch': sidecar['frames_total'] -= 1\n"
        "if scenario == 'missing_frame_times': sidecar.pop('frame_times_ms')\n"
        "if scenario == 'software_fallback': sidecar['job']['gpu']['software_encode_frames'] = 1\n"
        "if scenario == 'pipe_fallback': sidecar['job']['gpu']['video_pipe_fallback_frames'] = 1\n"
        "if scenario == 'missing_copy_counter': sidecar['job']['gpu'].pop('gpu_surface_copy_frames')\n"
        "if scenario == 'encoded_frames_mismatch': output.write_text('999')\n"
        "if scenario == 'encoded_fps_mismatch': output.write_text('fps=60/1;' + str(frame_count))\n"
        "pathlib.Path(str(output) + '.timing.json').write_text(json.dumps(sidecar))\n"
    )
    fake_cli.chmod(0o755)
    fake_ffprobe = fake_bin / "ffprobe"
    fake_ffprobe.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "text = pathlib.Path(sys.argv[-1]).read_text()\n"
        "if text.startswith('fps='): fps, count = text.split(';', 1)\n"
        "else: fps, count = 'fps=30/1', text\n"
        "print(json.dumps({'streams': [{'nb_read_frames': int(count), 'r_frame_rate': fps.split('=', 1)[1]}]}))\n"
    )
    fake_ffprobe.chmod(0o755)

    scenarios = {
        "pass": None,
        "missing_accounting": "wall-time attribution missing",
        "low_accounting": "below 95.00%",
        "missing_reread": "full_file_reread_count missing",
        "reread": "must be exactly 0",
        "surface_mismatch": "native GPU surfaces mismatch",
        "missing_downgrade": "execution_downgrade_count missing",
        "downgrade": "execution_downgrade_count missing or invalid",
        "handoff_fallback": "surface handoff must be direct",
        "missing_encode": "native NVENC frames mismatch",
        "encode_mismatch": "native NVENC frames mismatch",
        "readback": "CPU pixel readback bytes must be 0",
        "staging_copy": "encoder staging-copy bytes must be 0",
        "frame_count_mismatch": "frame count mismatch",
        "missing_frame_times": "frame count mismatch",
        "window_frame_mismatch": "prewarm/steady_state frame count mismatch",
        "software_fallback": "software encode fallback frames must be 0",
        "pipe_fallback": "video pipe fallback frames must be 0",
        "missing_copy_counter": "GPU surface-copy counter missing",
        "encoded_frames_mismatch": "encoded frame count mismatch",
        "encoded_fps_mismatch": "encoded frame rate mismatch",
    }
    with tempfile.TemporaryDirectory(prefix="gpu-corpus-contract-", dir=ROOT / ".tmp") as temp:
        for scenario, expected_error in scenarios.items():
            output_dir = Path(temp) / scenario
            env = os.environ.copy()
            env.update({
                "PATH": f"{fake_bin}{os.pathsep}{env['PATH']}",
                "CHRONON3D_ALLOW_DIRTY": "1",
                "CHRONON3D_CONTENT_PACK_ID": "test-fixture-only",
                "GPU_CORPUS_TEST_SCENARIO": scenario,
            })
            completed = subprocess.run(
                [
                    "bash", str(RUNNER), "--cli", str(fake_cli),
                    "--out", str(output_dir), "--case", "TEST=FakeScene",
                    "--frames", "2", "--warmup", "0",
                    "--backend", "vulkan", "--hardware", "nvenc",
                    "--encoder-backend", "native",
                    "--gpu-hot-path-mode", "require_gpu_native",
                ],
                env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            )
            report_path = output_dir / "gpu-baseline.json"
            assert report_path.is_file(), (scenario, completed.stderr)
            report = json.loads(report_path.read_text())
            case = report["cases"][0]
            if expected_error is None:
                assert completed.returncode == 0, (scenario, completed.stderr)
                assert case["error"] is None
                assert report["case_gate_pass"] is True
                assert report["certification_eligible"] is False
                assert "complete versioned corpus manifest was not run" in report["certification_blockers"]
            else:
                assert completed.returncode != 0, scenario
                assert expected_error in case["error"], (scenario, case["error"])

    with tempfile.TemporaryDirectory(prefix="gpu-corpus-preflight-", dir=ROOT / ".tmp") as temp:
        output_dir = Path(temp) / "preflight"
        env = os.environ.copy()
        env.update({
            "PATH": f"{fake_bin}{os.pathsep}{env['PATH']}",
            "CHRONON3D_ALLOW_DIRTY": "1",
            "CHRONON3D_CONTENT_PACK_ID": "test-fixture-only",
        })
        completed = subprocess.run(
            [
                "bash", str(RUNNER), "--cli", str(fake_cli),
                "--out", str(output_dir), "--corpus", str(MANIFEST),
                "--backend", "vulkan", "--hardware", "nvenc",
                "--encoder-backend", "native",
                "--gpu-hot-path-mode", "require_gpu_native",
            ],
            env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        report = json.loads((output_dir / "gpu-baseline.json").read_text())
        assert completed.returncode != 0
        assert len(report["cases"]) == 17
        assert all(c["error_class"] == "composition_not_registered" for c in report["cases"])
        assert report["case_gate_pass"] is False
        assert report["certification_eligible"] is False
        assert not any(Path(temp).rglob("render.mp4"))


if __name__ == "__main__":
    test_manifest_is_complete_and_unique()
    test_runner_declares_fail_closed_certification_gates()
    test_runner_closes_sidecar_contract_fail_closed()
    print("gpu corpus manifest tests: PASS")

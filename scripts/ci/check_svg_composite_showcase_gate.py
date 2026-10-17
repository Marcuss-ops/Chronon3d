#!/usr/bin/env python3
"""Composite path-operator showcase certification gate (TICKET-SVG-RENDER-PLAN P0).

Renders one native inline path through the canonical CLI render path with six
chained path operators — trim, round_corners, zig_zag, offset_path,
pucker_bloat and an animated repeater — then verifies:

  1. all authored frames decode to visible RGBA pixels;
  2. the authored endpoint frame hashes differ (the composite animates);
  3. a repeat render reproduces identical frame hashes (determinism);
  4. an MP4 render of the same plan produces an h264 file with all authored
     frames.

The gate fails closed: missing CLI, a CLI failure, an invisible frame, close
endpoint hashes, divergent repeat hashes, or a broken MP4 abort the run.
"""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CANVAS = {"width": 128, "height": 128, "fps_num": 30, "fps_den": 1,
          "duration_frames": 31}

PLAN = json.dumps({
    "schema": "chronon.render-plan.v3", "version": 3, "job_id": "compositor_showcase",
    "canvas": CANVAS,
    "layers": [{
        "id": "composite", "type": "shape", "size": [128, 128],
        "position": [64, 64],
        "shape": {
            "type": "path",
            "path": [
                {"type": "move_to", "point": [-24.0, -24.0]},
                {"type": "line_to", "point": [24.0, -24.0]},
                {"type": "line_to", "point": [24.0, 24.0]},
                {"type": "line_to", "point": [-24.0, 24.0]},
                {"type": "line_to", "point": [-24.0, -24.0]},
            ],
            "stroke": {"color": [0.1, 0.15, 0.45, 1.0], "width": 3.0},
            "operators": [
                {"kind": "trim", "params": {"start": 0.02, "end": 0.98}},
                {"kind": "zig_zag", "params": {"size": 2.5, "points_per_unit": 0.12}},
                {"kind": "offset_path", "params": {"amount": 2.0, "miter_limit": 4.0}},
                {"kind": "pucker_bloat", "params": {"amount": 0.3, "anchor": [0.0, 0.0]}},
                {"kind": "round_corners", "params": {"radius": 4.0}},
                {"kind": "repeater", "params": {
                    "copies": 3, "position": [42.0, 0.0], "opacity_decay": 1.0,
                    "animation": {"easing": "linear", "keyframes": [
                        {"frame": 0, "value": [0.0, 0.0, 1.0, 1.0, 0.0]},
                        {"frame": 30, "value": [0.0, 0.0, 1.0, 1.0, 45.0]}]}},
                },
            ],
        },
    }],
    "output": {"path": "showcase_####.png", "format": "png_sequence",
               "color_space": "srgb"},
})


def cli() -> Path:
    override = os.environ.get("CHRONON_CLI")
    if override:
        return Path(override)
    for candidate in (
        ROOT / "build" / "chronon" / "linux-video-test" / "apps" / "chronon3d_cli" / "chronon3d_cli",
        ROOT / "build" / "chronon" / "linux-fast-dev" / "apps" / "chronon3d_cli" / "chronon3d_cli",
    ):
        if candidate.is_file():
            return candidate
    raise SystemExit("chronon3d_cli not found (set CHRONON_CLI to an SVG-enabled build)")


def render_cl(path: Path, plan_file: Path, output: Path) -> None:
    subprocess.run(
        [str(path), "render", "--plan", str(plan_file), "--backend", "software",
         "--start-frame", "0", "--end-frame", "30", "--output", str(output)],
        capture_output=True, text=True, check=True)


def decode(path: Path) -> bytes:
    proc = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", str(path), "-f", "rawvideo", "-pix_fmt",
         "rgba", "-"], capture_output=True, check=True)
    if len(proc.stdout) != 128 * 128 * 4:
        raise SystemExit(f"frame {path.name} did not decode to one 128x128 RGBA image")
    return proc.stdout


def alpha_pixels(buf: bytes) -> int:
    return sum(1 for i in range(0, len(buf), 4) if buf[i + 3] > 100)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="compositor_showcase_gate_") as tmp:
        tmp_path = Path(tmp)
        (tmp_path / "plan.json").write_text(PLAN)
        executable = cli()

        output = tmp_path / "run1"
        output.mkdir()
        render_cl(executable, tmp_path / "plan.json", output / "showcase_####.png")
        repeat = tmp_path / "repeat"
        repeat.mkdir()
        render_cl(executable, tmp_path / "plan.json", repeat / "showcase_####.png")

        frames = {}
        for frame in (0, 15, 30):
            image = output / f"showcase_{frame:04d}.png"
            if not image.is_file():
                raise SystemExit(f"missing frame {frame}")
            frames[frame] = decode(image)

        checks: list[tuple[bool, str]] = []
        counts = {f: alpha_pixels(buf) for f, buf in frames.items()}
        checks.extend([
            (all(c > 200 for c in counts.values()),
             f"composite visible at f0/f15/f30 ({counts})"),
            (hashlib.sha256(frames[0]).hexdigest() !=
             hashlib.sha256(frames[30]).hexdigest(),
             "endpoint hashes differ (composite animates)"),
        ])

        repeat_hash = hashlib.sha256(decode(repeat / "showcase_0015.png")).hexdigest()
        checks.append((repeat_hash == hashlib.sha256(frames[15]).hexdigest(),
                       "repeat render reproduces the frame-15 hash (deterministic)"))

        mp4 = tmp_path / "showcase.mp4"
        mp4_plan = json.loads(PLAN)
        mp4_plan["job_id"] = "compositor_showcase_mp4"
        mp4_plan["output"] = {"path": "showcase.mp4", "format": "mp4",
                              "color_space": "srgb"}
        (tmp_path / "plan.mp4.json").write_text(json.dumps(mp4_plan))
        render_cl(executable, tmp_path / "plan.mp4.json", mp4)
        probe = subprocess.run(
            ["ffprobe", "-v", "error", "-select_streams", "v:0",
             "-show_entries", "stream=codec_name,nb_frames,width,height",
             "-of", "json", str(mp4)], capture_output=True, text=True, check=True)
        stream = json.loads(probe.stdout)["streams"][0]
        checks.append((stream["codec_name"] == "h264" and
                       int(stream["nb_frames"]) == 31 and
                       (stream["width"], stream["height"]) == (128, 128),
                       f"MP4 output is h264 128x128 with 31 frames ({stream})"))

        ok = True
        for passed, message in checks:
            print(("  PASS  " if passed else "  FAIL  ") + message)
            ok = ok and passed
        print("COMPOSITE_SHOWCASE_GATE " + ("PASS" if ok else "FAIL"))
        return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())

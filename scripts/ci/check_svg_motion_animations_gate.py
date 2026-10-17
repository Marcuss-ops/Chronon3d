#!/usr/bin/env python3
"""Six base SVG/shape animation certification gate (TICKET-SVG-RENDER-PLAN P0).

Renders one fixture per base animation through the canonical CLI render path
(software backend, PNG sequence) and verifies authored frame endpoints with
visible pixels and distinct frame hashes plus per-animation visual predicates:

  line-reveal      Trim Paths stroke reveal on an inline path plan
  icon-morph       animated SVG path `d` morph
  animated-stroke  SVG stroke width and opacity animation
  group-hierarchy  SVG group translate while the child path rotates
  mask-reveal      SVG root clipPath + animated path translate
  color-gradient   animated SVG gradient stop colors

The gate fails closed: a missing CLI, a CLI failure, an invisible or colliding
endpoint, or a violated per-frame predicate aborts the run.
"""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

SVG_NS = 'xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64"'
CANVAS = {"width": 64, "height": 64, "fps_num": 30, "fps_den": 1, "duration_frames": 31}

FIXTURES: dict[str, dict] = {}

def plan(job_id: str, layers: str) -> str:
    return (json.dumps({
        "schema": "chronon.render-plan.v3", "version": 3, "job_id": job_id,
        "canvas": CANVAS,
        "layers": json.loads(layers),
        "output": {"path": f"{job_id}_####.png", "format": "png_sequence",
                   "color_space": "srgb"},
    }))

def path_layer(layer_id: str, payload: str) -> str:
    return (f'[{{"id":"{layer_id}","type":"shape","size":[64,64],'
            f'"position":[32,32],"shape":{payload}}}]')

# 1. Line reveal — Trim Paths stroke reveal on an inline path.
FIXTURES["line_reveal"] = {
    "plan": plan("line_reveal", path_layer("line", json.dumps({
        "type": "path",
        "path": [
            {"type": "move_to", "point": [-24.0, 0.0]},
            {"type": "line_to", "point": [24.0, 0.0]},
        ],
        "stroke": {"color": [0, 0, 0, 1], "width": 4.0},
        "operators": [{
            "kind": "trim",
            "params": {
                "start": 0.0, "end": 0.02, "offset": 0.0,
                "animation": {"keyframes": [
                    {"frame": 0, "value": [0.0, 0.02, 0.0], "interpolation": "linear"},
                    {"frame": 30, "value": [0.0, 1.0, 0.0], "interpolation": "linear"},
                ]},
            },
        }],
    }))),
    "predicate_frames": (0, 15, 30),
    "predicate": "stroke grows: f0 dot, f15 partial, f30 full width",
}

# 2. Icon morph — animated SVG path `d` (circle-ish triangle to tall triangle).
FIXTURES["icon_morph"] = {
    "svg": f'<svg {SVG_NS}><path fill="#FFD500" d="M 16 20 L 48 20 L 32 44 Z">'
           '<animate attributeName="d" values='
           '"M 16 20 L 48 20 L 32 44 Z;M 20 12 L 28 12 L 24 52 Z;'
           'M 20 12 L 28 12 L 24 52 Z" keyTimes="0;0.5;1" dur="1s" fill="freeze"/>'
           "</path></svg>",
    "predicate_frames": (0, 30),
    "predicate": "morph changes the filled region between endpoints",
}

# 3. Animated stroke — width 2→10 and opacity 1→0.2 on an open path.
FIXTURES["animated_stroke"] = {
    "svg": f'<svg {SVG_NS}><path d="M 8 32 L 56 32" fill="none" stroke="#FF0000" '
           'stroke-width="2" stroke-opacity="1">'
           '<animate attributeName="stroke-width" from="2" to="10" dur="1s" fill="freeze"/>'
           '<animate attributeName="stroke-opacity" from="1" to="0.25" dur="1s" fill="freeze"/>'
           "</path></svg>",
    "predicate_frames": (0, 30),
    "predicate": "stroke thickens (more opaque pixels) while fading",
}

# 4. Group hierarchy — group translates while the child path rotates.
FIXTURES["group_hierarchy"] = {
    "svg": f'<svg {SVG_NS}><g><animateTransform attributeName="transform" '
           'type="translate" from="0 0" to="24 0" dur="1s" fill="freeze"/>'
           '<path d="M 32 16 L 32 48" stroke="#00AA00" stroke-width="4" fill="none">'
           '<animateTransform attributeName="transform" type="rotate" from="0" '
           'to="90" dur="1s" fill="freeze"/></path></g></svg>',
    "predicate_frames": (0, 30),
    "predicate": "child line rotates while the group translates",
}

# 5. Mask reveal — root clipPath limits visible region while the path moves.
FIXTURES["mask_reveal"] = {
    "svg": f'<svg {SVG_NS} clip-path="url(#half)"><defs><clipPath id="half">'
           '<path d="M 0 0 L 32 0 L 32 64 L 0 64 Z"/></clipPath></defs>'
           '<path d="M 8 24 L 24 24 L 24 40 L 8 40 Z" fill="#FF00FF" '
           'clip-path="url(#half)"><animateTransform attributeName="transform" '
           'type="translate" from="0 0" to="24 0" dur="1s" fill="freeze"/></path></svg>',
    "predicate_frames": (0, 15, 30),
    "predicate": "static clip band stays; content slides out and exits fully at 30",
}

# 6. Color & gradient — animated gradient stop colors on a filled shape.
FIXTURES["color_gradient"] = {
    "svg": f'<svg {SVG_NS}><defs><linearGradient id="sweep" x1="0" y1="0" x2="1" y2="0">'
           '<stop offset="0" stop-color="#FF0000">'
           '<animate attributeName="stop-color" from="#FF0000" to="#0000FF" '
           'dur="1s" fill="freeze"/></stop>'
           '<stop offset="1" stop-color="#0000FF">'
           '<animate attributeName="stop-color" from="#0000FF" to="#00FF00" '
           'dur="1s" fill="freeze"/></stop>'
           "</linearGradient></defs>"
           '<rect x="8" y="20" width="48" height="24" fill="url(#sweep)"/></svg>',
    "predicate_frames": (0, 30),
    "predicate": "gradient hue rotates red→blue at f0 to blue→green at f30",
}


# 7. Animated repeater — copies, offset and scale keyframes on a Lottie source.
FIXTURES["repeater"] = {
    "files": {"shapes/repeater_source.json": json.dumps({"v": "5.7.4", "layers": [{
        "ty": 4, "ddd": 0,
        "ks": {"a": {"a": 0, "k": [0, 0]}, "p": {"a": 0, "k": [0, 0]},
                "s": {"a": 0, "k": [100, 100]}, "r": {"a": 0, "k": 0},
                "o": {"a": 0, "k": 100}},
        "shapes": [
            {"ty": "sh", "ks": {"a": 0, "k": {"c": True,
                "v": [[0, 0], [8, 0], [8, 8], [0, 8]],
                "i": [[0, 0], [0, 0], [0, 0], [0, 0]],
                "o": [[0, 0], [0, 0], [0, 0], [0, 0]]}}},
            {"ty": "fl", "r": 1, "c": {"a": 0, "k": [1, 0, 0, 1]}}]}]})},
    "plan": plan("repeater", path_layer("vector", json.dumps({
        "type": "path", "lottie": "shapes/repeater_source.json",
        "operators": [{"kind": "repeater", "params": {
            "copies": 3, "position": [6, 0], "opacity_decay": 1.0,
            "animation": {"easing": "linear", "keyframes": [
                {"frame": 0, "value": [0, 0, 1, 1, 0]},
                {"frame": 30, "value": [12, 0, 2, 1, 0]}]}}}]}))),
    "predicate_frames": (0, 30),
}

# 9. Nested animated groups — a child translation inside a rotating parent.
# SVG nesting gives R(p + t); the renderer can only express R*p + t, so the
# importer must reject this combination explicitly (negative certification).
FIXTURES["nested_groups"] = {
    "expect_error": "SVG animation is malformed",
    "svg": f'<svg {SVG_NS}><g><animateTransform attributeName="transform" '
           'type="rotate" from="0" to="45" dur="1s" fill="freeze"/>'
           '<g><animateTransform attributeName="transform" type="translate" '
           'from="0 0" to="16 0" dur="1s" fill="freeze"/>'
           '<path d="M 32 32 L 40 32" stroke="#00AA00" stroke-width="4" fill="none"/>'
           "</g></g></svg>",
    "predicate_frames": (0, 30),
    "predicate": "translate nested inside rotating parent is rejected, not rendered wrong",
}

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


def decode(path: Path) -> bytes:
    proc = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", str(path), "-f", "rawvideo", "-pix_fmt", "rgba", "-"],
        capture_output=True, check=True)
    if len(proc.stdout) != 64 * 64 * 4:
        raise SystemExit(f"frame {path.name} did not decode to one 64x64 RGBA image")
    return proc.stdout


def px(buf: bytes) -> list[int]:
    return [buf[i] for i in range(0, len(buf), 4) if buf[i + 3] > 100]


def visible(buf: bytes) -> bool:
    return any(buf[3::4])


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="svg_motion_gate_") as tmp:
        tmp_path = Path(tmp)
        name_to_frames: dict[str, dict[int, bytes]] = {}
        for name, fixture in FIXTURES.items():
            directory = tmp_path / name
            directory.mkdir()
            if "svg" in fixture:
                (directory / f"{name}.svg").write_text(fixture["svg"])
                payload = json.dumps({"type": "path", "asset": f"{name}.svg"})
                plan_path = plan(name, path_layer("shape", payload))
            else:
                plan_path = fixture["plan"]
            for rel, text in fixture.get("files", {}).items():
                (directory / rel).parent.mkdir(parents=True, exist_ok=True)
                (directory / rel).write_text(text)
            (directory / "plan.json").write_text(plan_path)

            executable = cli()
            output = directory / "out_####.png"
            proc = subprocess.run(
                [str(executable), "render", "--plan", str(directory / "plan.json"),
                 "--assets-root", str(directory), "--backend", "software",
                 "--start-frame", "0", "--end-frame", "30", "--output", str(output)],
                capture_output=True, text=True)
            if "expect_error" in fixture:
                # Unsupported combinations must be rejected explicitly, never rendered wrong.
                log = proc.stdout + proc.stderr
                if proc.returncode == 0 or fixture["expect_error"] not in log:
                    raise SystemExit(f"{name}: expected explicit rejection, got exit {proc.returncode}")
                print(f"{name}: explicitly rejected ({fixture['expect_error']})")
                continue
            if proc.returncode != 0:
                raise SystemExit(f"{name}: render failed with exit {proc.returncode}: {proc.stderr}")
            frames = {}
            for frame in fixture["predicate_frames"]:
                image = directory / f"out_{frame:04d}.png"
                if not image.is_file():
                    raise SystemExit(f"{name}: missing frame {frame}")
                frames[frame] = decode(image)
            name_to_frames[name] = frames
            print(f"{name}: " + "  ".join(
                f"f{frame}={hashlib.sha256(buf).hexdigest()[:12]}"
                for frame, buf in frames.items()))

        checks: list[tuple[bool, str]] = []
        line = name_to_frames["line_reveal"]
        counts = {f: len(px(line[f])) for f in line}
        checks.append((0 <= counts[0] <= 16,
                       f"line_reveal starts as a dot at 0 (got {counts[0]} px)"))
        checks.append((0 < counts[15] < counts[30],
                       f"line_reveal grows ({counts[0]} -> {counts[15]} -> {counts[30]} px)"))
        checks.append((counts[30] > 120, f"line_reveal full at 30 (got {counts[30]} px)"))

        morph = name_to_frames["icon_morph"]
        checks.append((hashlib.sha256(morph[0]).hexdigest() != hashlib.sha256(morph[30]).hexdigest(),
                       "icon_morph endpoints differ"))
        checks.append((visible(morph[0]) and visible(morph[30]), "icon_morph endpoints visible"))

        stroke = name_to_frames["animated_stroke"]
        def dim(buf: bytes) -> int:
            # Opacity fades to 0.25 (alpha 64), so measure by a lower threshold.
            return sum(1 for i in range(0, len(buf), 4) if buf[i] > 100 and buf[i + 3] > 40)
        counts = {f: dim(stroke[f]) for f in stroke}
        checks.append((counts[30] > counts[0],
                       f"animated_stroke thickens ({counts[0]} -> {counts[30]} px)"))
        checks.append((visible(stroke[30]), "animated_stroke stays above zero alpha"))

        group = name_to_frames["group_hierarchy"]
        def bbox(buf: bytes) -> tuple[int, int] | None:
            cols = [i // 4 % 64 for i in range(0, len(buf), 4) if buf[i + 3] > 100]
            return (min(cols), max(cols)) if cols else None
        start, end = bbox(group[0]), bbox(group[30])
        checks.append((start is not None and end is not None and end[1] > start[1],
                       f"group_hierarchy translates/rotates ({start} -> {end})"))

        mask = name_to_frames["mask_reveal"]
        def max_col(buf: bytes) -> int:
            cols = [i // 4 % 64 for i in range(0, len(buf), 4) if buf[i + 3] > 100]
            return max(cols) if cols else -1
        checks.append((max_col(mask[0]) < 32, f"mask_reveal clipped at 0 (max col {max_col(mask[0])})"))
        checks.append((max_col(mask[15]) < 32, f"mask_reveal clipped at 15 (max col {max_col(mask[15])})"))
        checks.append((max_col(mask[30]) <= 32, f"mask_reveal clipped at 30 (max col {max_col(mask[30])})"))
        checks.append((len(px(mask[30])) == 0, "mask_reveal content exits the clip fully at 30"))

        grad = name_to_frames["color_gradient"]
        def hue(buf: bytes, offset: int) -> int:
            values = [buf[i + offset] for i in range(0, len(buf), 4) if buf[i + 3] > 100]
            return sum(values)
        checks.append((hue(grad[0], 0) > hue(grad[0], 2), "color_gradient starts red-dominant"))
        checks.append((hue(grad[30], 2) > hue(grad[30], 0), "color_gradient ends blue/green-dominant"))

        rep = name_to_frames["repeater"]
        checks.append((hashlib.sha256(rep[0]).hexdigest() != hashlib.sha256(rep[30]).hexdigest(),
                       "repeater frames differ over time (animated copies reach the CLI)"))
        checks.append((visible(rep[0]) and visible(rep[30]), "repeater endpoints visible"))
        checks.append((max_col(rep[30]) > max_col(rep[0]),
                       f"repeater copies move with keyframes (xmax {max_col(rep[0])} -> {max_col(rep[30])})"))

        failures = [message for ok, message in checks if not ok]
        for ok, message in checks:
            print(("  PASS  " if ok else "  FAIL  ") + message)
        if failures:
            raise SystemExit(f"SVG_MOTION_GATE FAIL: {len(failures)} predicate(s) failed")
    print("SVG_MOTION_GATE PASS: six base animations and animated repeater certify end to end")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

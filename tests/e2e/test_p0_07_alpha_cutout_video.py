#!/usr/bin/env python3
"""P0-07 — video E2E gate for alpha-cutout vs blending, two meshes and two planes.

Why this exists
---------------
P0-07 was scored PARTIAL because the alpha-test/cutout contract had unit and
software-pixel coverage but no moving-picture evidence. This gate closes that
gap on the canonical pre-encode lane (`--video-sink raw`): it renders the whole
clip through the real CLI, decodes every frame from the raw sink and asserts the
declared contract on pixels.

Contract under test
-------------------
1. Opaque mesh: its interior stays fully covered for the whole clip.
2. `alpha_cutoff = 0.5` on an opacity 0.2 -> 0.8 ramp: **binary discard**. Frames
   below the cutoff carry *zero* pixels of that mesh; frames above it carry the
   mesh, with a stable footprint (no semi-transparent ghost band).
3. Fractional alpha (0.5, no cutoff): the mesh is **blended**. A second render
   of the same clip with that mesh at opacity 1.0 gives the opaque reference, so
   the measured blend ratio is checked against 0.5 instead of guessed.
4. Two planes at different depths: the nearer plane is declared *before* the
   meshes yet its pixels survive inside the opaque mesh's projected footprint,
   so the composite orders by Z, not by layer order.

The fixture is `fixtures/p0-07-alpha-cutout-two-meshes.plan.json`; the two plane
textures are generated here so the gate carries no external assets.

Usage:
  python3 tests/e2e/test_p0_07_alpha_cutout_video.py \
      [--cli Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli]
      [--backend software|vulkan]
"""

from __future__ import annotations

import argparse
import copy
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
FIXTURE = HERE / "fixtures" / "p0-07-alpha-cutout-two-meshes.plan.json"
W, H, FRAMES = 640, 360, 24
CUTOFF_FRAME = 12  # 0.2 + 0.6 * 12/23 = 0.513 > 0.5
failures: list[str] = []


def check(condition: bool, message: str) -> None:
    print(("PASS " if condition else "FAIL ") + message)
    if not condition:
        failures.append(message)


def generate_assets(root: Path) -> None:
    Image.new("RGB", (16, 16), (8, 18, 56)).save(root / "backdrop.png")
    Image.new("RGB", (16, 16), (238, 206, 40)).save(root / "front.png")


def render(cli: Path, plan: Path, assets: Path, output: Path,
           backend: str, last_frame: int = FRAMES - 1) -> None:
    command = [str(cli), "render", "--plan", str(plan), "--assets-root", str(assets),
               "--backend", backend, "--start-frame", "0", "--end-frame", str(last_frame),
               "--video-sink", "raw", "--output", str(output), "-v", "error"]
    if backend == "vulkan":
        # Raw readback is intentional here: the fixture checks renderer output
        # before any encoder conversion. This keeps the GPU gate usable on a
        # Vulkan host even when that host has no NVENC/CUDA interop build.
        command += ["--gpu-hot-path-mode", "auto", "--encoder-backend", "pipe",
                    "--hardware", "none", "--codec", "libx264", "--pipe-pixfmt", "rgba"]
    subprocess.run(
        command,
        check=True,
    )


def load_frames(path: Path) -> np.ndarray:
    expected = W * H * 4 * FRAMES
    size = path.stat().st_size
    if size != expected:
        raise SystemExit(f"raw sink size {size} != {expected} (frames={FRAMES}, {W}x{H} RGBA8)")
    return np.memmap(path, dtype=np.uint8, mode="r", shape=(FRAMES, H, W, 4))


def masks(rgb: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    r, g, b = (rgb[..., i].astype(np.int16) for i in range(3))
    red = (r > 120) & (r > 2 * g) & (r > 2 * b)
    yellow = (r > 120) & (g > 110) & (b < 0.6 * np.minimum(r, g))
    green = (g > 90) & (g > 1.5 * r) & (g > 1.5 * b)
    cyan = (b > 120) & (b > g + 20) & (g > r + 20)
    return red, yellow, green, cyan


def bbox(mask: np.ndarray) -> tuple[int, int, int, int]:
    ys, xs = np.where(mask)
    if len(xs) == 0:
        raise SystemExit("fixture probe region is empty: the clip did not render the expected layers")
    return int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())


def inside(box: tuple[int, int, int, int], x: int, y: int) -> bool:
    return box[0] <= x <= box[2] and box[1] <= y <= box[3]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", default="build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli")
    parser.add_argument("--backend", choices=("software", "vulkan"), default="software")
    parser.add_argument("--keep", action="store_true", help="keep the rendered clips + probe frames")
    args = parser.parse_args()

    cli = Path(args.cli).resolve()
    if not cli.is_file():
        raise SystemExit(f"CLI not found: {cli}")

    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        assets = work / "assets"
        assets.mkdir()
        generate_assets(assets)

        plan = json.loads(FIXTURE.read_text())
        plan_path = work / "plan.json"
        plan_path.write_text(json.dumps(plan))
        raw = work / "clip.rgba"
        render(cli, plan_path, assets, raw, args.backend)

        # Opaque reference for the fractional mesh: same clip, opacity 1.0.
        reference = copy.deepcopy(plan)
        for layer in reference["layers"]:
            if layer["id"] == "fractional-cyan":
                layer["opacity"] = 1.0
                layer.pop("animation", None)
        reference_path = work / "reference.json"
        reference_path.write_text(json.dumps(reference))
        ref_raw = work / "reference.rgba"
        render(cli, reference_path, assets, ref_raw, args.backend)

        frames = load_frames(raw)
        ref_frames = load_frames(ref_raw)
        rgb0 = frames[0, ..., :3]
        red0, yellow0, _, cyan0 = masks(rgb0)

        red_box = bbox(red0)
        plane_box = bbox(yellow0)
        cyan_box = bbox(cyan0)
        plane_cx = (plane_box[0] + plane_box[2]) // 2
        plane_cy = (plane_box[1] + plane_box[3]) // 2
        cyan_cx = (cyan_box[0] + cyan_box[2]) // 2
        cyan_cy = (cyan_box[1] + cyan_box[3]) // 2
        print(f"red bbox={red_box} plane bbox={plane_box} cyan bbox={cyan_box}")

        # The nearer plane must sit inside the opaque mesh's projected footprint;
        # otherwise "it wins" would be trivially true.
        check(inside(red_box, plane_cx, plane_cy),
              "nearer plane overlaps the opaque mesh footprint")
        check(inside(red_box, cyan_cx, cyan_cy) is False,
              "fractional mesh does not overlap the opaque mesh footprint (clean probes)")

        # Pick a red probe as far as possible from both the nearer plane and
        # the fractional mesh, so neither antialiased edge tints the sample.
        def distance_to_box(x: int, y: int, box: tuple[int, int, int, int]) -> float:
            dx = max(box[0] - x, 0, x - box[2])
            dy = max(box[1] - y, 0, y - box[3])
            return float(np.hypot(dx, dy))

        red_ys, red_xs = np.where(red0)
        candidates = [(x, y) for x, y in zip(red_xs, red_ys)
                      if distance_to_box(x, y, plane_box) > 8 and
                         distance_to_box(x, y, cyan_box) > 8]
        if not candidates:
            raise SystemExit("no opaque-mesh probe outside the plane and fractional mesh")
        red_x, red_y = max(
            candidates,
            key=lambda p: min(distance_to_box(*p, plane_box), distance_to_box(*p, cyan_box)),
        )

        backdrop = rgb0[H - 10, W - 10].astype(np.int16)
        print(f"probes: red=({red_x},{red_y}) plane=({plane_cx},{plane_cy}) "
              f"cyan=({cyan_cx},{cyan_cy}) backdrop={tuple(int(v) for v in backdrop)}")
        check(backdrop[2] > backdrop[0] + 15 and backdrop[2] > backdrop[1] + 15,
              "backdrop is the blue plane behind the meshes")

        green_counts, red_counts, plane_counts = [], [], []
        for frame in range(FRAMES):
            red, yellow, green, _ = masks(frames[frame, ..., :3])
            green_counts.append(int(green.sum()))
            red_counts.append(int(red.sum()))
            plane_counts.append(int(yellow.sum()))

            check(bool(red[red_y, red_x]), f"f{frame}: opaque mesh covers its interior")
            check(bool(yellow[plane_cy, plane_cx]),
                  f"f{frame}: nearer plane declared before the mesh wins on overlap")

            alpha = 0.2 + 0.6 * frame / (FRAMES - 1)
            if frame < CUTOFF_FRAME:
                check(green_counts[-1] == 0,
                      f"f{frame}: alpha {alpha:.3f} < cutoff -> cutout fully discarded "
                      f"(green_px={green_counts[-1]})")
            elif frame > CUTOFF_FRAME:
                check(green_counts[-1] > 500,
                      f"f{frame}: alpha {alpha:.3f} > cutoff -> cutout drawn "
                      f"(green_px={green_counts[-1]})")


        check(max(green_counts[:CUTOFF_FRAME]) == 0,
              f"frames 0..{CUTOFF_FRAME - 1}: zero cutout pixels below threshold")
        check(min(green_counts[CUTOFF_FRAME + 1:]) > 500,
              f"frames {CUTOFF_FRAME + 1}..{FRAMES - 1}: cutout present above threshold")
        above = green_counts[CUTOFF_FRAME + 1:]
        check((max(above) - min(above)) / max(above) < 0.05,
              f"cutout footprint is binary-stable above threshold "
              f"({min(above)}..{max(above)} px)")
        check(min(red_counts) > 15000, f"opaque mesh coverage stable: min={min(red_counts)} px")
        check(min(plane_counts) > 8000, f"nearer plane coverage stable: min={min(plane_counts)} px")

        # Fractional alpha: compare the 0.5 mesh against its opacity-1.0 reference.
        half = float(frames[0, cyan_cy, cyan_cx, 2])
        full = float(ref_frames[0, cyan_cy, cyan_cx, 2])
        background = float(backdrop[2])
        ratio = (half - background) / (full - background) if full > background else float("nan")
        print(f"fractional alpha: b(0.5)={half:.0f} b(1.0)={full:.0f} "
              f"backdrop_b={background:.0f} -> measured blend ratio={ratio:.2f}")
        check(0.30 <= ratio <= 0.70,
              "fractional alpha 0.5 blends halfway between the backdrop and the opaque reference")
        check(half < full - 20,
              "fractional mesh is visibly lighter than its opaque reference")
        check(frames[0, cyan_cy, cyan_cx, 1] > backdrop[1] + 10,
              "fractional mesh is not discarded")

        # Closed-volume regression: a translucent box must not composite its
        # front and back surfaces (0.5 would read as 0.75 without culling).
        box_plan = copy.deepcopy(plan)
        box_plan["layers"] = [layer for layer in box_plan["layers"] if layer["id"] != "backdrop"]
        for layer in box_plan["layers"]:
            layer["duration_frames"] = 1
            layer.pop("animation", None)
            if layer["id"] == "fractional-cyan":
                layer["inline_mesh"]["primitive"] = {
                    "kind": "box", "width": 2, "height": 2, "depth": 2}
        box_plan["canvas"]["duration_frames"] = 1
        box_path = work / "box.json"
        box_path.write_text(json.dumps(box_plan))
        box_raw = work / "box.rgba"
        render(cli, box_path, assets, box_raw, args.backend, last_frame=0)
        box_frame = np.fromfile(box_raw, dtype=np.uint8).reshape(H, W, 4)
        box_alpha = int(box_frame[cyan_cy, cyan_cx, 3])
        print(f"closed-volume alpha at 0.5 opacity: {box_alpha}/255")
        check(118 <= box_alpha <= 138,
              f"translucent box draws one surface (alpha={box_alpha}/255, not 191)")

        if args.keep:
            keep = Path("reports/p0-07-alpha-cutout")
            keep.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(raw, keep / "clip.rgba")
            shutil.copyfile(ref_raw, keep / "reference.rgba")
            for frame in (0, CUTOFF_FRAME - 1, CUTOFF_FRAME, FRAMES - 1):
                Image.fromarray(frames[frame]).save(keep / f"frame-{frame:02d}.png")
            print("kept clips + frames in", keep)

    if failures:
        print(f"\n{len(failures)} check(s) failed")
        return 1
    print(f"\nall checks passed ({FRAMES} frames, alpha-cutout + fractional + 2 planes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

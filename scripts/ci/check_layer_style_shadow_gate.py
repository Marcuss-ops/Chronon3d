#!/usr/bin/env python3
"""Layer `style.shadow` / `style.shadows` lowering gate (card shadow P0).

`style.shadow` / `style.shadows` were decoded, validated and fingerprinted but
never lowered by `Compiler::compile`. `materialize_media_background_cards`
moved the declaration onto the `__frame` plate it creates for a media card, the
decoder accepted it, and the frame then dropped it on the floor: a card
declared with a shadow rendered exactly like the same card without one, with
no warning and no error. The fix lowers the declaration into a `DropShadow`
`EffectPlan` (`render_plan_media_background.cpp:lower_layer_style_shadows`).

This gate renders, through the canonical CLI render path and the raw
pre-encode sink (the repository's pixel-truth surface), the two shapes the
contract actually serves, and fails closed:

  1. media card — `style.background` + `style.shadow` on an image layer, i.e.
     the real card path where the plate owns the shadow: the shadow may only
     ADD alpha coverage (below/around the plate) and the opaque plate pixels
     must stay byte-identical;
  2. shape card on an opaque background — the shadow must darken the band
     under the card while the card body itself stays byte-identical;
  3. an invalid shadow colour must be rejected at compile time instead of
     rendering a silently shadow-less frame.

A shadow-less frame keeps the gate red for every prior revision of the code,
which is exactly the regression it exists to catch.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "golden" / "layer_style_shadow_gate"
CANVAS_W, CANVAS_H = 320, 240
FRAME_BYTES = CANVAS_W * CANVAS_H * 4

# Shape-card windows, in top-left pixel coordinates. The card is a 160x100
# rect at (80, 60) on an opaque white canvas, and its shadow is offset by
# [0, 12] with blur 6.
BODY = (slice(80, 150), slice(90, 230))
BELOW = (slice(160, 185), slice(75, 245))


def cli() -> Path:
    override = os.environ.get("CHRONON_CLI")
    if override:
        return Path(override)
    for candidate in (
        ROOT / "build" / "chronon" / "linux-video-release" / "apps" / "chronon3d_cli" / "chronon3d_cli",
        ROOT / "build" / "chronon" / "linux-video-test" / "apps" / "chronon3d_cli" / "chronon3d_cli",
        ROOT / "build" / "chronon" / "linux-fast-dev" / "apps" / "chronon3d_cli" / "chronon3d_cli",
    ):
        if candidate.is_file():
            return candidate
    raise SystemExit("FAIL: chronon3d_cli not found (set CHRONON_CLI)")


def backend() -> str:
    # The software lane is the byte-exact oracle the gate asserts on. The warm
    # daemon renders through Vulkan, so CHRONON_GATE_BACKEND=vulkan lets the
    # same gate certify the backend the production worker actually drives.
    return os.environ.get("CHRONON_GATE_BACKEND", "software")


def render_cl(path: Path, plan_file: Path, output: Path) -> subprocess.CompletedProcess:
    command = [
        str(path), "render", "--plan", str(plan_file),
        "--assets-root", str(FIXTURE),
        "--backend", backend(), "--start-frame", "0", "--end-frame", "0",
        "--video-sink", "raw", "--output", str(output),
    ]
    return subprocess.run(command, capture_output=True, text=True)


def read_frame(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) != FRAME_BYTES:
        raise SystemExit(f"FAIL: {path} carries {len(data)} bytes, expected {FRAME_BYTES}")
    return data


def pixels(frame: bytes) -> list[tuple[int, int, int, int]]:
    return [tuple(frame[i:i + 4]) for i in range(0, len(frame), 4)]


def window(frame: bytes, rows: slice, cols: slice) -> list[tuple[int, int, int, int]]:
    px = pixels(frame)
    return [px[y * CANVAS_W + x] for y in range(rows.start, rows.stop) for x in range(cols.start, cols.stop)]


def luma(pixel: tuple[int, int, int, int]) -> float:
    return (pixel[0] + pixel[1] + pixel[2]) / 3.0


def main() -> int:
    executable = cli()
    failures: list[str] = []

    with tempfile.TemporaryDirectory(prefix="layer_style_shadow_gate_") as tmp:
        tmp_path = Path(tmp)

        def render(name: str) -> bytes:
            output = tmp_path / name
            output.mkdir()
            result = render_cl(executable, FIXTURE / f"{name}.json", output / "frame.dmp")
            if result.returncode != 0:
                raise SystemExit(
                    f"FAIL: {name} render exited {result.returncode}\n"
                    f"stdout: {result.stdout[-2000:]}\nstderr: {result.stderr[-2000:]}"
                )
            return read_frame(output / "frame.dmp")

        # 1. media card: plate owns the shadow.
        plate_plain = render("plan_card_plain")
        plate_shadow = render("plan_card_shadow")
        plain_px, shadow_px = pixels(plate_plain), pixels(plate_shadow)
        changed = [i for i, (a, b) in enumerate(zip(plain_px, shadow_px)) if a != b]
        alpha_up = sum(1 for a, b in zip(plain_px, shadow_px) if b[3] > a[3])
        alpha_down = sum(1 for a, b in zip(plain_px, shadow_px) if b[3] < a[3])
        plate_drift = sum(1 for i in changed if plain_px[i][3] == 255)
        print(f"media card: changed={len(changed)} alpha_up={alpha_up} "
              f"alpha_down={alpha_down} opaque_drift={plate_drift}")
        if len(changed) < 1000:
            failures.append("media card shadow produced (almost) no pixels: the declaration was dropped")
        if alpha_up < 1000:
            failures.append("media card shadow added no alpha coverage outside the plate")
        if alpha_down:
            failures.append(f"media card shadow erased coverage on {alpha_down} pixels")
        if plate_drift:
            failures.append(f"media card shadow altered {plate_drift} opaque plate pixels")

        # 2. shape card: shadow darkens below, body untouched.
        shape_plain = render("plan_shape_plain")
        shape_shadow = render("plan_shape_shadow")
        body_plain, body_shadow = window(shape_plain, *BODY), window(shape_shadow, *BODY)
        if body_plain != body_shadow:
            failures.append("shape card body changed when only style.shadow was declared")
        plain_below = sum(luma(p) for p in window(shape_plain, *BELOW)) / len(window(shape_plain, *BELOW))
        shadow_below = sum(luma(p) for p in window(shape_shadow, *BELOW)) / len(window(shape_shadow, *BELOW))
        print(f"shape card: below-plate luma plain={plain_below:.2f} shadow={shadow_below:.2f}")
        if plain_below < 240.0:
            failures.append("shape card baseline is not the opaque white canvas the windows assume")
        if shadow_below > plain_below - 20.0:
            failures.append(
                f"shape card shadow did not darken below the card "
                f"(plain={plain_below:.2f}, shadow={shadow_below:.2f})"
            )

        # 2b. multiple shadows: every declared shadow is lowered, not just the
        # first one. The fixture puts a blue shadow at offset [-24, 6] and a red
        # one at [24, 6], so the two side bands must tint in opposite directions.
        shape_shadows = render("plan_shape_shadows")
        if window(shape_shadows, *BODY) != body_plain:
            failures.append("shape card body changed when style.shadows was declared")
        shadows_vs_single = sum(
            1 for x, y in zip(pixels(shape_shadow), pixels(shape_shadows)) if x != y
        )
        left = [p for p in window(shape_shadows, slice(70, 150), slice(50, 78))]
        right = [p for p in window(shape_shadows, slice(70, 150), slice(242, 270))]
        left_rgb = [sum(p[c] for p in left) / len(left) for c in range(3)]
        right_rgb = [sum(p[c] for p in right) / len(right) for c in range(3)]
        print("multiple shadows: vs single="
              f"{shadows_vs_single} left_rgb={[round(v, 1) for v in left_rgb]} "
              f"right_rgb={[round(v, 1) for v in right_rgb]}")
        if shadows_vs_single < 1000:
            failures.append("style.shadows rendered like a single shadow (extra entries dropped)")
        if not left_rgb[2] > left_rgb[0] + 10.0:
            failures.append(
                "the blue left-offset shadow did not tint the left band "
                f"(rgb={[round(v, 1) for v in left_rgb]})"
            )
        if not right_rgb[0] > right_rgb[2] + 10.0:
            failures.append(
                "the red right-offset shadow did not tint the right band "
                f"(rgb={[round(v, 1) for v in right_rgb]})"
            )

        # 2c. multiple shadows on the media card: the plate still owns them all.
        card_shadows = render("plan_card_shadows")
        card_shadow_px = pixels(card_shadows)
        card_changed = sum(1 for x, y in zip(plain_px, card_shadow_px) if x != y)
        card_alpha_down = sum(1 for x, y in zip(plain_px, card_shadow_px) if y[3] < x[3])
        card_plate_drift = sum(
            1 for x, y in zip(plain_px, card_shadow_px) if x != y and x[3] == 255
        )
        print(f"media card (3 shadows): changed={card_changed} "
              f"alpha_down={card_alpha_down} opaque_drift={card_plate_drift}")
        if card_changed < 1000:
            failures.append("media card style.shadows produced (almost) no pixels")
        if card_alpha_down:
            failures.append(f"media card style.shadows erased coverage on {card_alpha_down} pixels")
        if card_plate_drift:
            failures.append(f"media card style.shadows altered {card_plate_drift} opaque plate pixels")

        # 3. invalid shadow colour fails closed.
        invalid_out = tmp_path / "invalid"
        invalid_out.mkdir()
        invalid = render_cl(executable, FIXTURE / "plan_invalid_shadow.json", invalid_out / "frame.dmp")
        combined = f"{invalid.stdout}\n{invalid.stderr}"
        if invalid.returncode == 0:
            failures.append("an invalid style.shadow colour rendered successfully instead of failing")
        elif "shadow" not in combined:
            failures.append("invalid style.shadow was rejected without naming the shadow field")
        print(f"invalid shadow colour: exit={invalid.returncode}")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1
    print(f"PASS ({backend()}): style.shadow/style.shadows lower to rendered drop shadows "
          "on both card paths")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

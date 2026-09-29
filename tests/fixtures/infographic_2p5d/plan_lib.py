"""Shared helpers for the 2.5D infographic render-plan fixtures.

Everything here is *measured* against the v3 render-plan contract, not derived
from it: `tests/fixtures/infographic_2p5d/README.md` records the evidence for
each convention the helpers encode.

This module is imported by `generate.py` and by the `scene_*` builders.  Keep it
free of scene content and of filesystem side effects.
"""

from __future__ import annotations

import math
import os

# ── Canonical canvas ────────────────────────────────────────────────────────
W, H, FPS, FRAMES = 960, 540, 30, 60
FONT = "assets/fonts/Inter-Bold.ttf"

# Shape, particles, light, adjustment and mesh layers are v3 layer types, and the
# v2 layer contract fails closed on them (additionalProperties: false), so the
# fixtures declare v3 rather than writing a v2 header they would not honour.
SCHEMA = "chronon.render-plan.v3"
VERSION = 3

# ── Camera ──────────────────────────────────────────────────────────────────
# Every fixture that wants real depth on its 3D layers while leaving the 2D
# layout at 1:1 declares an explicit camera at this distance.  See
# `one_to_one_fov_deg` for why the field of view is derived instead of hard-coded.
CAMERA_DISTANCE = 1000.0


def one_to_one_fov_deg(height: float = H, distance: float = CAMERA_DISTANCE) -> float:
    """Field of view whose focal length makes the z=0 plane render 1:1.

    The resolver projects with `scale = focal_px / depth` where
    `focal_px = (height / 2) / tan(fov / 2)`.  A layer lying in the z=0 plane is
    at `depth == distance`, so scale 1 requires

        focal_px == distance   =>   fov == 2 * atan((height / 2) / distance)

    Deriving the value keeps the camera in sync with the canvas instead of
    baking a magic number that silently rescales every 2D layer.
    """
    return math.degrees(2.0 * math.atan((height / 2.0) / distance))


def one_to_one_camera(height: float = H, distance: float = CAMERA_DISTANCE) -> dict:
    """A head-on perspective camera that leaves z=0 geometry at 1:1."""
    return {
        "type": "perspective",
        "position": [0.0, 0.0, -distance],
        "rotation_deg": [0.0, 0.0, 0.0],
        "fov_deg": one_to_one_fov_deg(height, distance),
        "zoom": distance,
    }


# ── Colours ─────────────────────────────────────────────────────────────────
def rgba(hex_color: str, alpha: float = 1.0) -> list:
    """`#RRGGBB` -> the plan's `[r, g, b, a]` unit floats."""
    h = hex_color.lstrip("#")
    return [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)] + [alpha]


def linear_gradient(start, end, stops) -> dict:
    """Gradient geometry is normalised to the shape's own box in [0, 1]
    (`tests/c_abi/test_render_plan_shape_helpers.hpp` authors `start [0,0]` to
    `end [1,0]`), not in canvas pixels."""
    return {
        "type": "linear",
        "start": list(start),
        "end": list(end),
        "color_stops": [
            {"position": pos, "color": rgba(color, alpha)} for pos, color, alpha in stops
        ],
    }


# ── Animation ───────────────────────────────────────────────────────────────
def keyframe(frame: int, value, **extra) -> dict:
    out = {"frame": frame, "value": value}
    out.update(extra)
    return out


def track(prop: str, frames: list, easing: str = None) -> dict:
    out = {"property": prop, "keyframes": frames}
    if easing:
        out["easing"] = easing
    return out


def animation(*tracks: dict) -> dict:
    return {"tracks": list(tracks)}


def fade_in(start: int = 0, length: int = 12, easing: str = "out_cubic") -> dict:
    """An opacity ramp, the animation every non-hero layer shares."""
    return animation(track("opacity", [keyframe(start, 0.0), keyframe(start + length, 1.0)],
                           easing=easing))


# ── Layers ──────────────────────────────────────────────────────────────────
def at(cx: float, cy: float):
    """Author in canvas-centre coordinates, emit the contract's convention.

    A plan position is the layer *centre* in canvas pixels with the origin at the
    top-left, so `at(0, 0)` is the centre of the canvas.  Position *keyframes* are
    absolute in this same centre-origin space.
    """
    return (W / 2 + cx, H / 2 + cy)


def _base_layer(layer_id: str, layer_type: str, size, position, **extra) -> dict:
    layer = {
        "id": layer_id,
        "type": layer_type,
        "size": list(size),
        "position": list(position),
        "start_frame": 0,
        "duration_frames": FRAMES,
    }
    layer.update(extra)
    return layer


def shape_layer(layer_id: str, shape: dict, size, position, **extra) -> dict:
    """A shape layer needs `size` *and* explicit frame bounds to draw."""
    return _base_layer(layer_id, "shape", size, position, shape=shape, **extra)


def text_layer(layer_id: str, words: str, size, position, font_size: float,
               fill: str, **extra) -> dict:
    return _base_layer(
        layer_id, "text", size, position,
        text=words,
        style={"font": FONT, "font_size": font_size, "fill": fill},
        **extra,
    )


def color_layer(layer_id: str, size, position, fill: str, alpha: float = 1.0, **extra) -> dict:
    return _base_layer(layer_id, "color", size, position, color=rgba(fill, alpha), **extra)


# ── Paths ───────────────────────────────────────────────────────────────────
def bezier_commands(p0, p1, p2, p3) -> list:
    return [
        {"type": "move_to", "point": list(p0)},
        {"type": "cubic_to", "control1": list(p1), "control2": list(p2), "point": list(p3)},
    ]


def polyline_commands(points, close: bool = False) -> list:
    commands = [{"type": "move_to", "point": list(points[0])}]
    commands += [{"type": "line_to", "point": list(p)} for p in points[1:]]
    if close:
        commands.append({"type": "close"})
    return commands


def step_commands(xs, ys) -> list:
    """A staircase polyline: advance in x at the old level, then step the level.

    This is the shape an infographic step-line chart draws; authoring it as one
    `path` is what keeps a five-point chart inside a single layer.
    """
    commands = [{"type": "move_to", "point": [float(xs[0]), float(ys[0])]}]
    for i in range(1, len(xs)):
        commands.append({"type": "line_to", "point": [float(xs[i]), float(ys[i - 1])]})
        commands.append({"type": "line_to", "point": [float(xs[i]), float(ys[i])]})
    return commands


def cubic_at(p0, p1, p2, p3, t):
    u = 1.0 - t
    return [
        u * u * u * p0[i] + 3 * u * u * t * p1[i] + 3 * u * t * t * p2[i] + t * t * t * p3[i]
        for i in range(2)
    ]


def cubic_length(p0, p1, p2, p3, steps: int = 512) -> float:
    """Arc length of a cubic, sampled.  Used to size the write-on dash period."""
    total = 0.0
    prev = cubic_at(p0, p1, p2, p3, 0.0)
    for i in range(1, steps + 1):
        cur = cubic_at(p0, p1, p2, p3, i / steps)
        total += math.dist(prev, cur)
        prev = cur
    return total


# ── Plan envelope ───────────────────────────────────────────────────────────
def plan(job_id: str, layers: list, camera: dict = None, variables: dict = None,
         fps: int = FPS, frames: int = FRAMES) -> dict:
    out = {
        "schema": SCHEMA,
        "version": VERSION,
        "job_id": job_id,
        "canvas": {
            "width": W, "height": H,
            "fps_num": fps, "fps_den": 1, "duration_frames": frames,
        },
        "output": {"path": job_id + ".mp4", "format": "mp4", "codec": "h264"},
        "layers": layers,
    }
    if camera is not None:
        out["camera"] = camera
    if variables:
        out["variables"] = variables
    return out


def plans_dir() -> str:
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), "plans")

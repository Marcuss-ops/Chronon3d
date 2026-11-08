#!/usr/bin/env python3
"""Generate the deterministic artifacts used by the six-test broadcast gate.

Outputs (next to this tool's --output directory):

  broadcast_canary.plan.json   1080p30 four-second materiality/hierarchy canary
  broadcast_canary_morph.svg   topology-matched 12-point star -> dodecagon
  subpixel_4k.plan.json        3840x2160, 30 frames, 0.1 px/frame translation
  subpixel_720.plan.json       720x405, same motion in device pixels

The guide curves are selected from a small cinematic family by measuring the
same normalized velocity peak the diagnostic gate will measure.  The script
prints the chosen control points so the asset is auditable, not tuned by eye.
"""
from __future__ import annotations

import json
import math
from pathlib import Path
from typing import Iterable

HERE = Path(__file__).resolve().parent


def bezier_y_at_x(x: float, x1: float, y1: float, x2: float, y2: float) -> float:
    lo, hi = 0.0, 1.0
    for _ in range(40):
        t = (lo + hi) * 0.5
        xt = 3 * (1 - t) ** 2 * t * x1 + 3 * (1 - t) * t * t * x2 + t ** 3
        if xt < x:
            lo = t
        else:
            hi = t
    t = (lo + hi) * 0.5
    return 3 * (1 - t) ** 2 * t * y1 + 3 * (1 - t) * t * t * y2 + t ** 3


def curve_metrics(curve: tuple[float, float, float, float]) -> tuple[float, float, float]:
    x1, y1, x2, y2 = curve
    samples = 1601
    xs = [i / (samples - 1) for i in range(samples)]
    ys = [bezier_y_at_x(x, x1, y1, x2, y2) for x in xs]
    speeds = [abs(ys[i] - ys[i - 1]) * (samples - 1) for i in range(1, len(ys))]
    peak_index = max(range(len(speeds)), key=lambda i: speeds[i])
    peak_time = peak_index / (len(speeds) - 1)
    peak = speeds[peak_index]
    tail = sum(speeds[int(0.9 * len(speeds)):]) / len(speeds[int(0.9 * len(speeds)):])
    return peak_time, tail / peak, max(ys) - min(ys)


def choose_curve() -> tuple[tuple[float, float, float, float], tuple[float, float, float]]:
    family = [
        (0.16, 1.0, 0.30, 1.0),
        (0.22, 1.0, 0.36, 1.0),
        (0.30, 0.90, 0.22, 1.00),
        (0.35, 0.70, 0.20, 1.00),
        (0.38, 0.55, 0.18, 1.00),
        (0.42, 0.35, 0.18, 1.00),
        (0.45, 0.20, 0.16, 1.00),
        (0.40, 0.10, 0.20, 1.00),
        (0.50, 0.25, 0.20, 1.00),
        (0.34, 0.15, 0.24, 1.00),
    ]
    scored = []
    for curve in family:
        peak_time, tail_ratio, overshoot = curve_metrics(curve)
        if not (0.15 <= peak_time <= 0.25):
            continue
        if tail_ratio >= 0.05 or overshoot > 1.0001:
            continue
        scored.append((abs(peak_time - 0.20) + tail_ratio, curve,
                       (peak_time, tail_ratio, overshoot)))
    if not scored:
        raise SystemExit("no cinematic curve in the family satisfies the 15-25% peak window")
    scored.sort(key=lambda item: item[0])
    return scored[0][1], scored[0][2]


def rgba(hex_color: str) -> list[float]:
    hex_color = hex_color.lstrip("#")
    return [int(hex_color[i:i + 2], 16) / 255.0 for i in (0, 2, 4)] + [1.0]


def star_points(outer: float, inner: float, centre: tuple[float, float],
                count: int = 12) -> list[tuple[float, float]]:
    points = []
    for index in range(count):
        angle = math.radians(-90 + index * 360 / count)
        radius = outer if index % 2 == 0 else inner
        points.append((centre[0] + radius * math.cos(angle),
                       centre[1] + radius * math.sin(angle)))
    return points


def polygon_path(points: Iterable[tuple[float, float]]) -> str:
    pts = list(points)
    return "M " + " L ".join(f"{x:.4f} {y:.4f}" for x, y in pts) + " Z"


def build_morph_svg(path: Path) -> None:
    centre = (960.0, 540.0)
    star = star_points(170.0, 72.0, centre)
    dodecagon = star_points(150.0, 150.0, centre)
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="1920" height="1080" viewBox="0 0 1920 1080">
  <rect width="1920" height="1080" fill="#05070b"/>
  <path id="canary_morph" fill="#9fe8ff" fill-rule="nonzero" d="{polygon_path(star)}">
    <animate attributeName="d" dur="4s" keyTimes="0;1" fill="freeze"
             values="{polygon_path(star)};{polygon_path(dodecagon)}"/>
  </path>
</svg>
'''
    path.write_text(svg, encoding="utf-8")


def title_plan(curve: tuple[float, float, float, float]) -> dict:
    return {
        "id": "title",
        "type": "text",
        "text": "SIX TESTS",
        "size": [1200, 220],
        "position": [930, 495],
        "start_frame": 1,
        "duration_frames": 119,
        "style": {
            "font": "tests/fixtures/Inter-Bold.ttf",
            "font_size": 150,
            "fill": "#EAF4FF",
        },
        "animation": {
            "tracks": [
                {
                    "property": "scale",
                    "easing": "bezier",
                    "bezier_curve": list(curve),
                    "keyframes": [
                        {"frame": 1, "value": [0.94, 0.94]},
                        {"frame": 91, "value": [1.0, 1.0]},
                    ],
                },
                {
                    "property": "opacity",
                    "easing": "out_cubic",
                    "keyframes": [
                        {"frame": 1, "value": 0.0},
                        {"frame": 20, "value": 1.0},
                    ],
                },
            ]
        },
    }


def build_canary_plan(path: Path, curve: tuple[float, float, float, float]) -> None:
    card = {
        "id": "card",
        "type": "shape",
        "size": [980, 520],
        "position": [-30, 20],
        "start_frame": 0,
        "duration_frames": 120,
        "shape": {
            "type": "rounded_rect",
            "radius": 28,
            "fill": [0.055, 0.095, 0.14, 1.0],
        },
        "effects": [
            {
                "type": "catalog",
                "effect_id": "light.sdf_bevel",
                "params": [
                    {"param": "bevel_width", "value": 16.0},
                    {"param": "bevel_height", "value": 12.0},
                    {"param": "rim_width", "value": 3.0},
                    {"param": "rim_intensity", "value": 0.75},
                    {"param": "highlight", "value": 0.65},
                    {"param": "shade", "value": 0.42},
                    {"param": "light_angle", "value": -135.0},
                    {"param": "light_elevation", "value": 38.0},
                    {"param": "shadow_distance", "value": 12.0},
                    {"param": "shadow_softness", "value": 6.0},
                    {"param": "shadow_opacity", "value": 0.75},
                ],
            }
        ],
        "animation": {
            "tracks": [
                {
                    "property": "opacity",
                    "easing": "out_cubic",
                    "keyframes": [
                        {"frame": 0, "value": 0.0},
                        {"frame": 10, "value": 1.0},
                    ],
                }
            ]
        },
    }
    underline = {
        "id": "underline",
        "type": "shape",
        "size": [1920, 1080],
        "position": [-30, 250],
        "start_frame": 2,
        "duration_frames": 118,
        "shape": {
            "type": "path",
            "path": [
                {"type": "move_to", "point": [-380.0, 0.0]},
                {"type": "line_to", "point": [380.0, 0.0]},
            ],
            "stroke": {"color": rgba("#42D9FF"), "width": 6.0},
            "fill": [0.0, 0.0, 0.0, 0.0],
            "operators": [
                {
                    "kind": "trim",
                    "params": {
                        "start": 0.0,
                        "end": 0.0,
                        "offset": 0.0,
                        "animation": {
                            "keyframes": [
                                {
                                    "frame": 2,
                                    "value": [0.0, 0.0, 0.0],
                                    "easing": "bezier",
                                    "bezier_curve": list(curve),
                                },
                                {
                                    "frame": 8,
                                    "value": [0.0, 1.0, 0.0],
                                    "easing": "in_cubic",
                                },
                                {"frame": 32, "value": [1.0, 1.0, 0.0]},
                            ]
                        },
                    },
                }
            ],
        },
        "animation": {
            "tracks": [
                {
                    "property": "opacity",
                    "easing": "linear",
                    "keyframes": [
                        {"frame": 2, "value": 0.0},
                        {"frame": 5, "value": 1.0},
                    ],
                }
            ]
        },
    }
    badge = {
        "id": "badge",
        "type": "shape",
        "size": [180, 180],
        "position": [490, -220],
        "start_frame": 3,
        "duration_frames": 117,
        "shape": {
            "type": "ellipse",
            "fill": [0.85, 0.55, 0.12, 1.0],
        },
        "effects": [
            {
                "type": "catalog",
                "effect_id": "light.sdf_bevel",
                "params": [
                    {"param": "bevel_width", "value": 7.0},
                    {"param": "bevel_height", "value": 5.0},
                    {"param": "rim_width", "value": 1.5},
                    {"param": "rim_intensity", "value": 0.5},
                    {"param": "highlight", "value": 0.5},
                    {"param": "shade", "value": 0.35},
                    {"param": "light_angle", "value": -135.0},
                    {"param": "light_elevation", "value": 38.0},
                    {"param": "shadow_distance", "value": 0.0},
                    {"param": "shadow_opacity", "value": 0.0},
                ],
            }
        ],
        "animation": {
            "tracks": [
                {
                    "property": "scale",
                    "easing": "bezier",
                    "bezier_curve": list(curve),
                    "keyframes": [
                        {"frame": 3, "value": [0.70, 0.70]},
                        {"frame": 33, "value": [1.0, 1.0]},
                    ],
                },
                {
                    "property": "opacity",
                    "easing": "out_cubic",
                    "keyframes": [
                        {"frame": 3, "value": 0.0},
                        {"frame": 10, "value": 1.0},
                    ],
                },
            ]
        },
    }
    corner_ticks = {
        "id": "corner_ticks",
        "type": "shape",
        "size": [1920, 1080],
        "position": [0, 0],
        "start_frame": 0,
        "duration_frames": 120,
        "shape": {
            "type": "path",
            "path": [
                {"type": "move_to", "point": [-900.0, -500.0]},
                {"type": "line_to", "point": [-900.0, -460.0]},
                {"type": "line_to", "point": [-860.0, -460.0]},
                {"type": "move_to", "point": [860.0, -500.0]},
                {"type": "line_to", "point": [860.0, -460.0]},
                {"type": "line_to", "point": [900.0, -460.0]},
                {"type": "move_to", "point": [-900.0, 500.0]},
                {"type": "line_to", "point": [-900.0, 460.0]},
                {"type": "line_to", "point": [-860.0, 460.0]},
                {"type": "move_to", "point": [860.0, 500.0]},
                {"type": "line_to", "point": [860.0, 460.0]},
                {"type": "line_to", "point": [900.0, 460.0]},
            ],
            "stroke": {"color": [1.0, 1.0, 1.0, 1.0], "width": 5.0},
            "fill": [0.0, 0.0, 0.0, 0.0],
        },
        "effects": [
            {
                "type": "chromatic_aberration",
                "red_offset": 0.0,
                "blue_offset": 0.0,
                "red_transform": {
                    "scale": [1.0004, 1.0004],
                    "translation": [0.0, 0.0],
                },
                "blue_transform": {
                    "scale": [0.9996, 0.9996],
                    "translation": [0.0, 0.0],
                },
            }
        ],
    }
    contact_shadow = {
        "id": "contact_shadow",
        "type": "shape",
        "size": [1920, 1080],
        "position": [0, 0],
        "start_frame": 0,
        "duration_frames": 120,
        "opacity": 0.7,
        "shape": {
            "type": "path",
            "path": [
                {"type": "move_to", "point": [-500.0, -220.0]},
                {"type": "line_to", "point": [480.0, -220.0]},
                {"type": "line_to", "point": [480.0, 300.0]},
                {"type": "line_to", "point": [-500.0, 300.0]},
                {"type": "close"},
            ],
            "fill": [0.0, 0.0, 0.0, 1.0],
        },
        "effects": [{"type": "gaussian_blur", "radius": 8}],
    }
    film_grain = {
        "id": "film_grain",
        "type": "color",
        "color": [0.0, 0.0, 0.0, 1.0],
        "size": [1920, 1080],
        "position": [0, 0],
        "start_frame": 0,
        "duration_frames": 120,
        "opacity": 0.006,
        "blend_mode": "screen",
        "effects": [
            {
                "type": "catalog",
                "effect_id": "generate.noise",
                "params": [{"param": "amount", "value": 0.85}],
            }
        ],
    }
    plan = {
        "schema": "chronon.render-plan.v3",
        "version": 3,
        "job_id": "broadcast_six_test_canary",
        "canvas": {
            "width": 1920,
            "height": 1080,
            "fps_num": 30,
            "fps_den": 1,
            "duration_frames": 120,
        },
        "layers": [
            {
                "id": "backdrop",
                "type": "color",
                "color": [0.030, 0.042, 0.052, 1.0],
                "size": [1920, 1080],
                "position": [0, 0],
                "start_frame": 0,
                "duration_frames": 120,
                "effects": [
                    {
                        "type": "vignette",
                        "radius": 0.70,
                        "softness": 0.70,
                        "amount": 0.42,
                        "color": [0.0, 0.0, 0.0, 1.0],
                    }
                ],
            },
            contact_shadow,
            card,
            title_plan(curve),
            underline,
            badge,
            corner_ticks,
            film_grain,
        ],
        "output": {
            "path": "broadcast_six_test_canary_1080p.mp4",
            "format": "mp4",
            "codec": "h264",
            "color_space": "srgb",
        },
    }
    path.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")


def build_hierarchy_plan(path: Path) -> None:
    """60 fps, text-free four-layer cascade for the saccadic-gaze test."""
    plan = {
        "schema": "chronon.render-plan.v3",
        "version": 3,
        "job_id": "broadcast_hierarchy_60fps",
        "canvas": {
            "width": 960,
            "height": 540,
            "fps_num": 60,
            "fps_den": 1,
            "duration_frames": 120,
        },
        "layers": [
            {
                "id": "backdrop",
                "type": "color",
                "color": [0.018, 0.024, 0.032, 1.0],
                "size": [960, 540],
                "position": [0, 0],
                "start_frame": 0,
                "duration_frames": 120,
                "effects": [
                    {"type": "vignette", "radius": 0.72, "softness": 0.7,
                     "amount": 0.35, "color": [0, 0, 0, 1]}
                ],
            },
            {
                "id": "base_card",
                "type": "shape",
                "size": [420, 240],
                "position": [0, 30],
                "start_frame": 0,
                "duration_frames": 120,
                "shape": {"type": "rounded_rect", "radius": 18,
                          "fill": [0.02, 0.03, 0.04, 1.0]},
                "animation": {"tracks": [
                    {"property": "scale", "easing": "out_cubic",
                     "keyframes": [{"frame": 0, "value": [0.98, 0.98]},
                                   {"frame": 20, "value": [1.0, 1.0]}]}
                ]},
            },
            {
                "id": "core_icon",
                "type": "shape",
                "size": [110, 110],
                "position": [-260, 40],
                "start_frame": 2,
                "duration_frames": 118,
                "shape": {"type": "ellipse", "fill": [0.35, 0.85, 1.0, 1.0]},
                "animation": {"tracks": [
                    {"property": "scale", "easing": "linear",
                     "keyframes": [{"frame": 2, "value": [0.01, 0.01]},
                                   {"frame": 42, "value": [1.0, 1.0]}]},
                ]},
            },
            {
                "id": "data_bars",
                "type": "shape",
                "size": [220, 180],
                "position": [160, 40],
                "start_frame": 4,
                "duration_frames": 116,
                "shape": {
                    "type": "path",
                    "path": [
                        {"type": "move_to", "point": [-90.0, 30.0]},
                        {"type": "line_to", "point": [-90.0, -60.0]},
                        {"type": "line_to", "point": [-52.0, -60.0]},
                        {"type": "line_to", "point": [-52.0, 30.0]},
                        {"type": "close"},
                        {"type": "move_to", "point": [-26.0, 30.0]},
                        {"type": "line_to", "point": [-26.0, -20.0]},
                        {"type": "line_to", "point": [12.0, -20.0]},
                        {"type": "line_to", "point": [12.0, 30.0]},
                        {"type": "close"},
                        {"type": "move_to", "point": [38.0, 30.0]},
                        {"type": "line_to", "point": [38.0, -80.0]},
                        {"type": "line_to", "point": [76.0, -80.0]},
                        {"type": "line_to", "point": [76.0, 30.0]},
                        {"type": "close"},
                    ],
                    "fill": [0.88, 0.95, 1.0, 1.0],
                },
                "animation": {"tracks": [
                    {"property": "scale", "easing": "linear",
                     "keyframes": [{"frame": 4, "value": [0.01, 0.01]},
                                   {"frame": 44, "value": [1.0, 1.0]}]}
                ]},
            },
            {
                "id": "highlight",
                "type": "shape",
                "size": [80, 80],
                "position": [340, -30],
                "start_frame": 6,
                "duration_frames": 114,
                "shape": {"type": "rounded_rect", "radius": 16,
                          "fill": [0.90, 0.58, 0.14, 1.0]},
                "animation": {"tracks": [
                    {"property": "scale", "easing": "linear",
                     "keyframes": [{"frame": 6, "value": [0.01, 0.01]},
                                   {"frame": 46, "value": [1.0, 1.0]}]}
                ]},
            },
        ],
        "output": {
            "path": "broadcast_hierarchy_60fps.mp4",
            "format": "mp4",
            "codec": "h264",
        },
    }
    path.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")


def build_subpixel_plan(path: Path, width: int, height: int, label: str) -> None:
    seam = 0.76 * width
    seam_overlap = 0.5
    left_w = 0.06 * width + seam_overlap
    right_w = 0.06 * width + seam_overlap
    rect_y = 0.40 * height
    rect_h = 0.20 * height
    line_width = 3.0
    # The layer transform is the only animation channel the shape path
    # reliably evaluates today.  Offset the line inside a full-frame layer
    # and scale that layer so the line sweeps exactly 3 px; the scale also
    # changes the 3 px thickness by <2.7 %, far below the 4 % CV limit.
    vertical_offset = width / 2 - 0.26 * width
    vertical_scale = 1.0 - 3.0 / vertical_offset
    horizontal_offset = 0.78 * height - height / 2
    horizontal_scale = 1.0 + 3.0 / horizontal_offset
    plan = {
        "schema": "chronon.render-plan.v3",
        "version": 3,
        "job_id": f"broadcast_subpixel_{label}",
        "canvas": {
            "width": width,
            "height": height,
            "fps_num": 30,
            "fps_den": 1,
            "duration_frames": 30,
        },
        "layers": [
            {
                "id": "backdrop",
                "type": "color",
                "color": [0.0, 0.0, 0.0, 1.0],
                "size": [width, height],
                "position": [0, 0],
                "start_frame": 0,
                "duration_frames": 30,
            },
            {
                "id": "vertical_line",
                "type": "shape",
                "size": [width, height],
                "position": [0.0, 0.0],
                "start_frame": 0,
                "duration_frames": 30,
                "shape": {
                    "type": "path",
                    "path": [
                        {"type": "move_to", "point": [0.26 * width - width / 2, -0.15 * height]},
                        {"type": "line_to", "point": [0.26 * width - width / 2, 0.15 * height]},
                    ],
                    "stroke": {"color": [1.0, 1.0, 1.0, 1.0], "width": line_width},
                    "fill": [0.0, 0.0, 0.0, 0.0],
                },
                "animation": {
                    "tracks": [
                        {
                            "property": "scale",
                            "easing": "linear",
                            "keyframes": [
                                {"frame": 0, "value": [1.0, 1.0]},
                                {"frame": 29, "value": [vertical_scale, vertical_scale]},
                            ],
                        }
                    ]
                },
            },
            {
                "id": "horizontal_line",
                "type": "shape",
                "size": [width, height],
                "position": [0.0, 0.0],
                "start_frame": 0,
                "duration_frames": 30,
                "shape": {
                    "type": "path",
                    "path": [
                        {"type": "move_to", "point": [-0.15 * width, 0.78 * height - height / 2]},
                        {"type": "line_to", "point": [0.15 * width, 0.78 * height - height / 2]},
                    ],
                    "stroke": {"color": [1.0, 1.0, 1.0, 1.0], "width": line_width},
                    "fill": [0.0, 0.0, 0.0, 0.0],
                },
                "animation": {
                    "tracks": [
                        {
                            "property": "scale",
                            "easing": "linear",
                            "keyframes": [
                                {"frame": 0, "value": [1.0, 1.0]},
                                {"frame": 29, "value": [horizontal_scale, horizontal_scale]},
                            ],
                        }
                    ]
                },
            },
            {
                "id": "left_plate",
                "type": "shape",
                "size": [left_w, rect_h],
                "position": [(0.70 * width + seam + seam_overlap) / 2 - width / 2, rect_y - height / 2],
                "start_frame": 0,
                "duration_frames": 30,
                "shape": {
                    "type": "rect",
                    "fill": [0.9, 0.9, 0.9, 1.0],
                },
            },
            {
                "id": "right_plate",
                "type": "shape",
                "size": [right_w, rect_h],
                "position": [(seam - seam_overlap + 0.82 * width) / 2 - width / 2, rect_y - height / 2],
                "start_frame": 0,
                "duration_frames": 30,
                "shape": {
                    "type": "rect",
                    "fill": [0.9, 0.9, 0.9, 1.0],
                },
            },
        ],
        "output": {
            "path": f"broadcast_subpixel_{label}.mp4",
            "format": "mp4",
            "codec": "h264",
            "color_space": "srgb",
        },
    }
    path.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=HERE.parent / "docs/evidence/broadcast-six-test-20261108")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    curve, metrics = choose_curve()
    print(f"cinematic curve: cubic-bezier({curve[0]}, {curve[1]}, {curve[2]}, {curve[3]})")
    print(f"  peak_time={metrics[0]:.4f} tail_speed_ratio={metrics[1]:.5f} "
          f"y_range={metrics[2]:.5f}")

    build_morph_svg(args.output / "broadcast_canary_morph.svg")
    build_canary_plan(args.output / "broadcast_canary.plan.json", curve)
    build_hierarchy_plan(args.output / "broadcast_hierarchy_60fps.plan.json")
    build_subpixel_plan(args.output / "subpixel_4k.plan.json", 3840, 2160, "4k")
    build_subpixel_plan(args.output / "subpixel_720.plan.json", 720, 405, "720")
    print(f"artifacts written to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

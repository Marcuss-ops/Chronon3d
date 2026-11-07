#!/usr/bin/env python3
"""Generate the 1920x1080 2.5D Abstract + Cameraing Glossy Gel Text RenderPlan."""

import json
import math
from pathlib import Path


def ease_in_out_cubic(t: float) -> float:
    t = max(0.0, min(1.0, t))
    return 4.0 * t * t * t if t < 0.5 else 1.0 - pow(-2.0 * t + 2.0, 3.0) / 2.0


def ease_out_cubic(t: float) -> float:
    t = max(0.0, min(1.0, t))
    return 1.0 - pow(1.0 - t, 3.0)


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def build_camera_samples(duration_frames: int) -> list[dict]:
    """Build 240 cinematic 2.5D perspective camera samples with orbital sweep, dolly push-in, roll, and DoF rack."""
    samples = []
    for f in range(duration_frames):
        u = f / float(duration_frames - 1)
        # 3-act camera choreography:
        # Act 1 (0.00 -> 0.35): dramatic sweeping 3D entrance from wide angled orbit (-260, -95, -1380)
        #                       pushing in toward the hero gel plane at z=0, racking focus from foreground orbs to z=0
        # Act 2 (0.35 -> 0.72): intimate grazing hero orbit across the glossy gel letters (-60 -> +115 X, -840 Z)
        #                       showing 3D parallax against the deep abstract aurora/ribbons
        # Act 3 (0.72 -> 1.00): editorial crane + pull-back reveal (+115 -> -35 X, -840 -> -1080 Z) with subtle dutch roll
        if u < 0.35:
            k = ease_out_cubic(u / 0.35)
            cam_x = lerp(-120.0, -25.0, k)
            cam_y = lerp(-65.0, -10.0, k)
            cam_z = lerp(-2100.0, -1750.0, k)
            pitch = lerp(1.8, 0.4, k)
            yaw = lerp(4.5, 1.2, k)
            roll = 0.0
            fov = lerp(32.0, 28.0, k)
            focus_dist = abs(cam_z)
            f_stop = 2.4
            max_blur = 12.0
        elif u < 0.72:
            k = ease_in_out_cubic((u - 0.35) / 0.37)
            cam_x = lerp(-25.0, 65.0, k)
            cam_y = lerp(-10.0, 18.0, k)
            cam_z = lerp(-1750.0, -1650.0, math.sin(k * math.pi))
            pitch = lerp(0.4, -0.8, k)
            yaw = lerp(1.2, -3.5, k)
            roll = 0.0
            fov = lerp(28.0, 26.5, math.sin(k * math.pi))
            focus_dist = abs(cam_z)
            f_stop = 2.4
            max_blur = 12.0
        else:
            k = ease_in_out_cubic((u - 0.72) / 0.28)
            cam_x = lerp(65.0, -10.0, k)
            cam_y = lerp(18.0, -5.0, k)
            cam_z = lerp(-1750.0, -2000.0, k)
            pitch = lerp(-0.8, 0.2, k)
            yaw = lerp(-3.5, 0.8, k)
            roll = 0.0
            fov = lerp(28.0, 29.5, k)
            focus_dist = abs(cam_z)
            f_stop = 2.4
            max_blur = 12.0

        samples.append({
            "frame": float(f),
            "camera": {
                "type": "perspective",
                "position": [round(cam_x, 4), round(cam_y, 4), round(cam_z, 4)],
                "rotation_deg": [round(pitch, 4), round(yaw, 4), round(roll, 4)],
                "fov_deg": round(fov, 4),
                "near": 10.0,
                "far": 8000.0,
                "zoom": 1000.0,
                "dof": {
                    "enabled": False,
                    "focus_distance": round(focus_dist, 3),
                    "f_stop": round(f_stop, 3),
                    "max_blur": round(max_blur, 3),
                },
            },
        })
    return samples


def radial_orb_layer(
    layer_id: str,
    duration_frames: int,
    radius_px: float,
    pos_xyz: list[float],
    inner_rgba: list[float],
    mid_rgba: list[float],
    outer_rgba: list[float] | None = None,
    drift_dx: float = 90.0,
    drift_dy: float = -55.0,
    drift_dz: float = 0.0,
    rot_xyz: list[float] | None = None,
    scale_xyz: list[float] | None = None,
    opacity: float = 0.92,
    blend_mode: str = "screen",
) -> dict:
    if outer_rgba is None:
        outer_rgba = [inner_rgba[0], inner_rgba[1], inner_rgba[2], 0.0]
    diameter = radius_px * 2.0
    tracks = [
        {
            "property": "position_x",
            "easing": "in_out_sine",
            "keyframes": [
                {"frame": 0, "value": pos_xyz[0]},
                {"frame": duration_frames // 2, "value": pos_xyz[0] + drift_dx},
                {"frame": duration_frames - 1, "value": pos_xyz[0] - drift_dx * 0.35},
            ],
        },
        {
            "property": "position_y",
            "easing": "in_out_sine",
            "keyframes": [
                {"frame": 0, "value": pos_xyz[1]},
                {"frame": duration_frames // 2, "value": pos_xyz[1] + drift_dy},
                {"frame": duration_frames - 1, "value": pos_xyz[1] - drift_dy * 0.4},
            ],
        },
    ]
    if abs(drift_dz) > 1e-3:
        tracks.append({
            "property": "position_z",
            "easing": "in_out_sine",
            "keyframes": [
                {"frame": 0, "value": pos_xyz[2]},
                {"frame": duration_frames // 2, "value": pos_xyz[2] + drift_dz},
                {"frame": duration_frames - 1, "value": pos_xyz[2]},
            ],
        })
    return {
        "id": layer_id,
        "type": "shape",
        "start_frame": 0,
        "duration_frames": duration_frames,
        "enable_3d": True,
        "blend_mode": blend_mode,
        "opacity": opacity,
        "size": [diameter, diameter],
        "position": pos_xyz,
        "rotation": rot_xyz or [0.0, 0.0, 0.0],
        "scale": scale_xyz or [1.0, 1.0, 1.0],
        "shape": {
            "type": "ellipse",
            "fill": {
                "type": "radial",
                "center": [0.5, 0.5],
                "radius": 0.5,
                "color_stops": [
                    {"position": 0.0, "color": inner_rgba},
                    {"position": 0.48, "color": mid_rgba},
                    {"position": 1.0, "color": outer_rgba},
                ],
            },
        },
        "animation": {"tracks": tracks},
    }


def neon_ribbon_layer(
    layer_id: str,
    duration_frames: int,
    pos_xyz: list[float],
    rot_xyz: list[float],
    stroke_width: float,
    color_a: list[float],
    color_b: list[float],
    color_c: list[float],
    y_offset: float = 0.0,
    curve_amp: float = 190.0,
    drift_rot_z: float = 8.0,
    opacity: float = 0.82,
) -> dict:
    return {
        "id": layer_id,
        "type": "shape",
        "start_frame": 0,
        "duration_frames": duration_frames,
        "enable_3d": True,
        "blend_mode": "screen",
        "opacity": opacity,
        "size": [1920.0, 1080.0],
        "position": pos_xyz,
        "rotation": rot_xyz,
        "scale": [1.0, 1.0, 1.0],
        "shape": {
            "type": "path",
            "path": [
                {"type": "move_to", "point": [-900.0, y_offset + curve_amp * 0.45]},
                {
                    "type": "cubic_to",
                    "control1": [-520.0, y_offset - curve_amp],
                    "control2": [420.0, y_offset + curve_amp * 1.15],
                    "point": [900.0, y_offset - curve_amp * 0.35],
                },
            ],
            "stroke": {
                "width": stroke_width,
                "cap": "round",
                "join": "round",
                "gradient": {
                    "type": "linear",
                    "start": [0.0, 0.5],
                    "end": [1.0, 0.5],
                    "color_stops": [
                        {"position": 0.0, "color": [color_a[0], color_a[1], color_a[2], 0.0]},
                        {"position": 0.22, "color": color_a},
                        {"position": 0.52, "color": color_b},
                        {"position": 0.80, "color": color_c},
                        {"position": 1.0, "color": [color_c[0], color_c[1], color_c[2], 0.0]},
                    ],
                },
            },
        },
        "effects": [
            {
                "type": "glow",
                "color": [color_b[0], color_b[1], color_b[2], 0.85],
                "intensity": 1.15,
                "radius": 28.0,
            }
        ],
        "animation": {
            "tracks": [
                {
                    "property": "rotation_z",
                    "easing": "in_out_sine",
                    "keyframes": [
                        {"frame": 0, "value": rot_xyz[2]},
                        {"frame": duration_frames // 2, "value": rot_xyz[2] + drift_rot_z},
                        {"frame": duration_frames - 1, "value": rot_xyz[2] - drift_rot_z * 0.5},
                    ],
                },
                {
                    "property": "position_y",
                    "easing": "in_out_sine",
                    "keyframes": [
                        {"frame": 0, "value": pos_xyz[1]},
                        {"frame": duration_frames // 2, "value": pos_xyz[1] - 42.0},
                        {"frame": duration_frames - 1, "value": pos_xyz[1] + 24.0},
                    ],
                },
            ]
        },
    }


def build_plan() -> dict:
    duration_frames = 150  # 5.0 seconds @ 30fps
    camera_samples = build_camera_samples(duration_frames)

    layers = [
        # 0. Screen-space deep cosmic navy base so no pixel is ever unseeded
        {
            "id": "backdrop",
            "type": "image",
            "asset": "docs/evidence/gel-text-vulkan-smoke-20261105/backdrop.png",
            "screen_space": True,
            "size": [1920, 1080],
            "position": [0, 0],
            "fit": "cover",
            "start_frame": 0,
            "duration_frames": duration_frames,
        },
        # 1. Far 3D horizon aurora blooms (Z = +780 .. +520) — deep parallax & heavy DoF bokeh
        radial_orb_layer(
            layer_id="far_aurora_indigo_nw",
            duration_frames=duration_frames,
            radius_px=820.0,
            pos_xyz=[-420.0, -240.0, 760.0],
            inner_rgba=[0.11, 0.24, 0.82, 0.72],
            mid_rgba=[0.05, 0.11, 0.44, 0.34],
            drift_dx=140.0,
            drift_dy=75.0,
            drift_dz=-60.0,
            opacity=0.95,
        ),
        radial_orb_layer(
            layer_id="far_aurora_violet_se",
            duration_frames=duration_frames,
            radius_px=760.0,
            pos_xyz=[460.0, 230.0, 680.0],
            inner_rgba=[0.28, 0.12, 0.78, 0.62],
            mid_rgba=[0.09, 0.05, 0.35, 0.28],
            drift_dx=-130.0,
            drift_dy=-80.0,
            drift_dz=50.0,
            opacity=0.90,
        ),
        radial_orb_layer(
            layer_id="far_aurora_cyan_core",
            duration_frames=duration_frames,
            radius_px=680.0,
            pos_xyz=[40.0, -30.0, 540.0],
            inner_rgba=[0.04, 0.56, 0.96, 0.58],
            mid_rgba=[0.02, 0.22, 0.58, 0.24],
            drift_dx=-95.0,
            drift_dy=65.0,
            drift_dz=-45.0,
            opacity=0.88,
        ),
        # 2. Tilted 3D abstract orbital rings & perspective grid planes (Z = +440 .. +280)
        {
            "id": "abstract_orbital_ring_back",
            "type": "shape",
            "start_frame": 0,
            "duration_frames": duration_frames,
            "enable_3d": True,
            "blend_mode": "screen",
            "opacity": 0.65,
            "size": [1320.0, 1320.0],
            "position": [-120.0, 40.0, 420.0],
            "rotation": [62.0, -14.0, 18.0],
            "scale": [1.15, 0.48, 1.0],
            "shape": {
                "type": "ellipse",
                "stroke": {
                    "width": 7.0,
                    "gradient": {
                        "type": "linear",
                        "start": [0.0, 0.0],
                        "end": [1.0, 1.0],
                        "color_stops": [
                            {"position": 0.0, "color": [0.10, 0.88, 1.00, 0.85]},
                            {"position": 0.5, "color": [0.16, 0.42, 0.98, 0.25]},
                            {"position": 1.0, "color": [0.58, 0.28, 1.00, 0.78]},
                        ],
                    },
                },
            },
            "effects": [
                {
                    "type": "glow",
                    "color": [0.10, 0.72, 1.00, 0.85],
                    "intensity": 1.1,
                    "radius": 24.0,
                }
            ],
            "animation": {
                "tracks": [
                    {
                        "property": "rotation_z",
                        "easing": "linear",
                        "keyframes": [
                            {"frame": 0, "value": 18.0},
                            {"frame": duration_frames - 1, "value": 64.0},
                        ],
                    },
                    {
                        "property": "rotation_y",
                        "easing": "in_out_sine",
                        "keyframes": [
                            {"frame": 0, "value": -14.0},
                            {"frame": duration_frames // 2, "value": 12.0},
                            {"frame": duration_frames - 1, "value": -14.0},
                        ],
                    },
                ]
            },
        },
        # 3. Sweeping 3D liquid-neon bezier ribbons behind the title (Z = +340, +210)
        neon_ribbon_layer(
            layer_id="abstract_neon_ribbon_deep",
            duration_frames=duration_frames,
            pos_xyz=[0.0, -70.0, 340.0],
            rot_xyz=[18.0, -9.0, -7.0],
            stroke_width=18.0,
            color_a=[0.04, 0.52, 0.98, 0.78],
            color_b=[0.24, 0.94, 1.00, 0.92],
            color_c=[0.42, 0.26, 0.98, 0.75],
            y_offset=-60.0,
            curve_amp=230.0,
            drift_rot_z=9.5,
            opacity=0.80,
        ),
        neon_ribbon_layer(
            layer_id="abstract_neon_ribbon_mid",
            duration_frames=duration_frames,
            pos_xyz=[30.0, 85.0, 210.0],
            rot_xyz=[-14.0, 11.0, 6.0],
            stroke_width=13.0,
            color_a=[0.32, 0.22, 0.96, 0.70],
            color_b=[0.08, 0.82, 1.00, 0.88],
            color_c=[0.52, 0.96, 1.00, 0.76],
            y_offset=75.0,
            curve_amp=-185.0,
            drift_rot_z=-8.0,
            opacity=0.76,
        ),
        # 4. Midground floating luminous glass spheres (Z = +160 .. +110)
        radial_orb_layer(
            layer_id="mid_glass_orb_left",
            duration_frames=duration_frames,
            radius_px=210.0,
            pos_xyz=[-520.0, -155.0, 165.0],
            inner_rgba=[0.72, 0.98, 1.00, 0.82],
            mid_rgba=[0.08, 0.62, 0.98, 0.38],
            drift_dx=70.0,
            drift_dy=48.0,
            drift_dz=-35.0,
            opacity=0.84,
        ),
        radial_orb_layer(
            layer_id="mid_glass_orb_right",
            duration_frames=duration_frames,
            radius_px=240.0,
            pos_xyz=[545.0, 145.0, 135.0],
            inner_rgba=[0.54, 0.92, 1.00, 0.78],
            mid_rgba=[0.14, 0.36, 0.96, 0.34],
            drift_dx=-82.0,
            drift_dy=-52.0,
            drift_dz=30.0,
            opacity=0.82,
        ),
        # 5. Secondary 2.5D depth kicker text plane behind hero (Z = +95)
        {
            "id": "kicker_depth_plane",
            "type": "text",
            "start_frame": 0,
            "duration_frames": duration_frames,
            "enable_3d": True,
            "position": [960.0, 345.0, 95.0],
            "rotation": [0.0, 0.0, 0.0],
            "scale": [1.0, 1.0, 1.0],
            "size": [1920, 1080],
            "text": "CHRONON 2.5D OPTICAL GEL SURFACE",
            "style": {
                "font": "assets/fonts/Inter-Bold.ttf",
                "font_size": 34.0,
                "fill": "#A8ECFF",
            },
            "effects": [
                {
                    "type": "glow",
                    "color": [0.10, 0.68, 1.00, 0.85],
                    "intensity": 0.95,
                    "radius": 18.0,
                }
            ],
            "animation": {
                "tracks": [
                    {
                        "property": "position_z",
                        "easing": "out_cubic",
                        "keyframes": [
                            {"frame": 0, "value": 240.0},
                            {"frame": int(duration_frames * 0.22), "value": 95.0},
                            {"frame": duration_frames - 1, "value": 75.0},
                        ],
                    },
                    {
                        "property": "opacity",
                        "easing": "out_cubic",
                        "keyframes": [
                            {"frame": 0, "value": 0.15},
                            {"frame": int(duration_frames * 0.15), "value": 0.82},
                            {"frame": duration_frames - 1, "value": 0.82},
                        ],
                    },
                ]
            },
        },
        # 6. HERO 2.5D COMPOSITE GLOSSY GEL TEXT PLANE (Z = 0)
        #    "calories or ingredients" with composite SDF gel dome, sweeping grazing light,
        #    2.5D dolly + subtle 3D counter-tilt, and dual-halo cyan + royal-blue HDR bloom.
        {
            "id": "hero_gel_25d_title",
            "type": "text",
            "start_frame": 0,
            "duration_frames": duration_frames,
            "enable_3d": True,
            "position": [960.0, 535.0, 0.0],
            "rotation": [0.0, 0.0, 0.0],
            "scale": [1.0, 1.0, 1.0],
            "size": [1920, 1080],
            "text": "calories or ingredients",
            "style": {
                "font": "assets/fonts/Inter-Bold.ttf",
                "font_size": 112,
                "fill": "#00E5FF",
                "gel_surface": True
            },
            "text_animators": [
                {
                    "id": "hero_tracking_sweep",
                    "selectors": [
                        {
                            "unit": "line",
                            "shape": "square",
                            "order": "forward",
                            "combine": "replace",
                            "exclude_spaces": True
                        }
                    ],
                    "properties": [
                        {
                            "property": "tracking",
                            "easing": "linear",
                            "keyframes": [
                                {"frame": 0, "value": 4},
                                {"frame": int(duration_frames * 0.25), "value": 0},
                                {"frame": int(duration_frames * 0.55), "value": -16},
                                {"frame": int(duration_frames * 0.78), "value": 8},
                                {"frame": duration_frames - 1, "value": 0}
                            ]
                        }
                    ]
                }
            ],
            "effects": [
                {
                    "type": "glow",
                    "color": [0.18, 0.90, 1.00, 1.0],
                    "intensity": 1.18,
                    "radius": 22.0,
                },
                {
                    "type": "glow",
                    "color": [0.04, 0.36, 1.00, 1.0],
                    "intensity": 1.45,
                    "radius": 68.0,
                },
            ],
            "animation": {
                "tracks": [
                    {
                        "property": "position_z",
                        "easing": "out_cubic",
                        "keyframes": [
                            {"frame": 0, "value": -220.0},
                            {"frame": int(duration_frames * 0.22), "value": 0.0},
                            {"frame": int(duration_frames * 0.75), "value": 0.0},
                            {"frame": duration_frames - 1, "value": -35.0},
                        ],
                    },
                    {
                        "property": "rotation_y",
                        "easing": "in_out_sine",
                        "keyframes": [
                            {"frame": 0, "value": -8.5},
                            {"frame": int(duration_frames * 0.28), "value": 2.2},
                            {"frame": int(duration_frames * 0.65), "value": -3.4},
                            {"frame": duration_frames - 1, "value": 1.0},
                        ],
                    },
                    {
                        "property": "rotation_x",
                        "easing": "in_out_sine",
                        "keyframes": [
                            {"frame": 0, "value": 4.0},
                            {"frame": int(duration_frames * 0.38), "value": -1.5},
                            {"frame": duration_frames - 1, "value": 1.2},
                        ],
                    },
                ]
            },
        },
        # 7. Secondary 2.5D foreground subtitle plane (Z = -75) for extra depth layering
        {
            "id": "subtitle_depth_plane",
            "type": "text",
            "start_frame": 0,
            "duration_frames": duration_frames,
            "enable_3d": True,
            "position": [960.0, 725.0, -75.0],
            "rotation": [0.0, 0.0, 0.0],
            "scale": [1.0, 1.0, 1.0],
            "size": [1920, 1080],
            "text": "COMPOSITE SDF DOME  •  DUAL-HALO HDR BLOOM  •  2.5D CAMERA RIG & DOF",
            "style": {
                "font": "assets/fonts/Inter-Regular.ttf",
                "font_size": 26.0,
                "fill": "#C8F4FF",
            },
            "effects": [
                {
                    "type": "glow",
                    "color": [0.06, 0.52, 1.00, 0.80],
                    "intensity": 0.75,
                    "radius": 14.0,
                }
            ],
            "animation": {
                "tracks": [
                    {
                        "property": "position_z",
                        "easing": "out_cubic",
                        "keyframes": [
                            {"frame": 0, "value": -340.0},
                            {"frame": int(duration_frames * 0.25), "value": -75.0},
                            {"frame": duration_frames - 1, "value": -95.0},
                        ],
                    }
                ]
            },
        },
        # 8. Foreground out-of-focus 2.5D bokeh orbs passing near the lens (Z = -240, -310)
        radial_orb_layer(
            layer_id="fg_bokeh_orb_cyan_left",
            duration_frames=duration_frames,
            radius_px=150.0,
            pos_xyz=[-640.0, 290.0, -250.0],
            inner_rgba=[0.30, 0.95, 1.00, 0.52],
            mid_rgba=[0.06, 0.48, 0.98, 0.18],
            drift_dx=165.0,
            drift_dy=-95.0,
            drift_dz=-30.0,
            opacity=0.68,
        ),
        radial_orb_layer(
            layer_id="fg_bokeh_orb_blue_right",
            duration_frames=duration_frames,
            radius_px=175.0,
            pos_xyz=[680.0, -275.0, -290.0],
            inner_rgba=[0.22, 0.68, 1.00, 0.46],
            mid_rgba=[0.08, 0.24, 0.92, 0.15],
            drift_dx=-175.0,
            drift_dy=88.0,
            drift_dz=25.0,
            opacity=0.62,
        ),
    ]


    return {
        "schema": "chronon.render-plan.v3",
        "version": 3,
        "job_id": "gel-25d-abstract-camera-1080p",
        "canvas": {
            "width": 1920,
            "height": 1080,
            "fps_num": 30,
            "fps_den": 1,
            "duration_frames": duration_frames,
        },
        "camera": camera_samples[0]["camera"],
        "camera_samples": camera_samples,
        "output": {
            "path": "docs/evidence/gel-01-core-bloom-20261105/gel-25d-abstract-camera-1080p.mp4",
            "format": "mp4",
            "codec": "h264",
        },
        "layers": layers,
    }


if __name__ == "__main__":
    out_path = Path(__file__).resolve().parent / "gel-25d-abstract-camera-1080p.plan.json"
    plan = build_plan()
    out_path.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {out_path} ({len(plan['layers'])} layers, {len(plan['camera_samples'])} camera samples)")

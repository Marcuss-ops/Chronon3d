#!/usr/bin/env python3
"""Reproducible Chronon3D 20-case rendered showcase suite."""
from __future__ import annotations
import argparse, hashlib, json, math, pathlib, resource, shutil, subprocess, sys, time

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
MANIFEST_PATH = HERE / "manifest.json"
CLI_DEFAULT = REPO / "Chronon3d/build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli"
PALETTE = ["#42D9FF", "#FF6B8A", "#A98BFF", "#FFD166", "#52E0A0", "#F4F7FF"]


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def rgb(color):
    return [int(color[i:i+2], 16) / 255 for i in (1, 3, 5)] + [1.0]


def run(command, cwd=REPO, log=None):
    completed = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, check=False)
    if log:
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(completed.stdout, encoding="utf-8")
    if completed.returncode:
        raise RuntimeError(f"exit {completed.returncode}: {' '.join(map(str, command))}\n{completed.stdout[-3000:]}")
    return completed.stdout


def scene(case, output):
    width, height, fps = 1920, 1080, 30
    frames = case["duration_seconds"] * fps
    slug, ident = case["slug"], case["id"]
    layers = [{"id": "background", "type": "shape", "size": [width, height],
               "position": [width/2, height/2], "start_frame": 0, "duration_frames": frames,
               "shape": {"type": "rect", "fill": [8/255, 13/255, 24/255, 1]}}]

    def shape(name, x, y, color, size=(100, 100), rotation=0, tracks=None, shape_type="ellipse", z=0.0, blend_mode="normal"):
        item = {"id": name, "type": "shape", "size": list(size),
                "position": [x, y, z] if z != 0.0 else [x, y],
                "rotation": [0, 0, rotation], "enable_3d": True, "start_frame": 0, "duration_frames": frames,
                "shape": {"type": shape_type, "fill": rgb(color)}}
        if blend_mode != "normal":
            item["blend_mode"] = blend_mode
        if tracks:
            item["animation"] = {"tracks": tracks}
        return item

    def stroked_shape(name, x, y, stroke_color, stroke_width, size=(100, 100), fill_color=None, tracks=None, shape_type="rect", z=0.0):
        shape_payload = {"type": shape_type, "stroke": {"color": stroke_color, "width": stroke_width}}
        if fill_color:
            shape_payload["fill"] = rgb(fill_color)
        item = {"id": name, "type": "shape", "size": list(size),
                "position": [x, y, z] if z != 0.0 else [x, y],
                "enable_3d": True, "start_frame": 0, "duration_frames": frames,
                "shape": shape_payload}
        if tracks:
            item["animation"] = {"tracks": tracks}
        return item

    def text(name, label, x, y, size, color, tracks=None, z=0.0):
        item = {"id": name, "type": "text", "size": [1500, size*2],
                "position": [x, y, z] if z != 0.0 else [x, y],
                "start_frame": 0, "duration_frames": frames, "text": label,
                "style": {"font": "assets/fonts/Inter-Bold.ttf", "font_size": size, "fill": color}}
        if tracks:
            item["animation"] = {"tracks": tracks}
        return item

    layers.extend([
        text("suite_title", f"{ident}  {case['name'].upper()}", 110, 125, 46, "#F4F7FF"),
        text("suite_scope", f"CHRONON3D  /  {case['status']} INTEGRATION TEST ({case['duration_seconds']}s @ 30 FPS)", 110, 185, 20, "#8292AE")
    ])

    for i in range(8):
        x, y = 220 + (i % 4)*490, 440 + (i // 4)*260
        begin = int(i*frames/16)
        finish = min(frames-1, begin+max(2, fps//2))
        tracks = [{"property": "position", "easing": "in_out_cubic", "keyframes": [
            {"frame": begin, "value": [x-80, y]}, {"frame": finish, "value": [x, y]}]},
                  {"property": "opacity", "easing": "out_cubic", "keyframes": [
            {"frame": begin, "value": 0.15}, {"frame": finish, "value": 1.0}]}]
        layers.append(shape(f"accent_{i:02d}", x, y, PALETTE[i%6], (28+(i%4)*8,)*2, i*11, tracks))

    if slug == "glowing_path_follow":
        layers.append({
            "id": "bezier_trail", "type": "shape", "size": [width, height],
            "position": [width/2, height/2], "enable_3d": True, "start_frame": 0, "duration_frames": frames,
            "shape": {
                "type": "path",
                "path": [
                    {"type": "move_to", "point": [-700, 150]},
                    {"type": "cubic_to", "control1": [-300, -350], "control2": [250, 350], "point": [700, -150]}
                ],
                "stroke": {"color": "#42D9FF", "width": 8, "dash_array": [2400, 2400], "dash_offset": 2400}
            },
            "animation": {
                "tracks": [{
                    "property": "stroke_dash_offset", "easing": "in_out_cubic",
                    "keyframes": [{"frame": 0, "value": 2400}, {"frame": frames - 1, "value": 0}]
                }]
            }
        })
        t_samples = [0.0, 0.25, 0.5, 0.75, 1.0]
        p_keys = []
        rot_keys = []
        for s_idx, t in enumerate(t_samples):
            f = int(s_idx * (frames - 1) / (len(t_samples) - 1))
            bx = (1-t)**3 * 260 + 3*(1-t)**2 * t * 660 + 3*(1-t)*t**2 * 1210 + t**3 * 1660
            by = (1-t)**3 * 690 + 3*(1-t)**2 * t * 190 + 3*(1-t)*t**2 * 890 + t**3 * 390
            dx = 3*(1-t)**2 * (660-260) + 6*(1-t)*t * (1210-660) + 3*t**2 * (1660-1210)
            dy = 3*(1-t)**2 * (190-690) + 6*(1-t)*t * (890-190) + 3*t**2 * (390-890)
            angle = math.degrees(math.atan2(dy, dx))
            p_keys.append({"frame": f, "value": [round(bx, 1), round(by, 1)]})
            rot_keys.append({"frame": f, "value": round(angle, 1)})
        layers.append(shape("particle_halo", 260, 690, "#42D9FF", (70, 70), tracks=[
            {"property": "position", "easing": "in_out_cubic", "keyframes": p_keys},
            {"property": "opacity", "easing": "linear", "keyframes": [{"frame": 0, "value": 0.4}, {"frame": frames-1, "value": 0.8}]}
        ], blend_mode="add"))
        layers.append(shape("particle_core", 260, 690, "#F4F7FF", (26, 26), tracks=[
            {"property": "position", "easing": "in_out_cubic", "keyframes": p_keys},
            {"property": "rotation", "easing": "in_out_cubic", "keyframes": [{"frame": v["frame"], "value": [0, 0, v["value"]]} for v in rot_keys]}
        ]))

    elif slug == "procedural_light_trails":
        for i in range(5):
            y_shift = (i - 2) * 80
            c = PALETTE[i % 5]
            w = 5 + i * 2.5
            start_f = i * 10
            end_f = frames - 15 + i * 3
            layers.append({
                "id": f"light_trail_{i}", "type": "shape", "size": [width, height],
                "position": [width/2, height/2], "enable_3d": True, "start_frame": 0, "duration_frames": frames,
                "shape": {
                    "type": "path",
                    "path": [
                        {"type": "move_to", "point": [-820, y_shift - 160]},
                        {"type": "cubic_to", "control1": [-300, y_shift + 280], "control2": [280, y_shift - 280], "point": [820, y_shift + 160]}
                    ],
                    "stroke": {"color": c, "width": w, "dash_array": [2500, 2500], "dash_offset": 2500}
                },
                "animation": {
                    "tracks": [
                        {"property": "stroke_dash_offset", "easing": "in_out_cubic",
                         "keyframes": [{"frame": start_f, "value": 2500}, {"frame": end_f, "value": 0}]},
                        {"property": "opacity", "easing": "linear",
                         "keyframes": [{"frame": start_f, "value": 0.2}, {"frame": int((start_f+end_f)/2), "value": 1.0}, {"frame": frames-1, "value": 0.3}]}
                    ]
                }
            })

    elif slug == "particle_vortex":
        layers.append({
            "id": "vortex_emitter", "type": "particles", "size": [width, height], "position": [width/2, height/2],
            "duration_frames": frames,
            "particle_emitter": {
                "count": 250, "seed": 20261012, "emitter_size": [650, 650],
                "velocity_min": [-6, -6], "velocity_max": [6, 6], "lifetime": [2, 6], "size": [3, 14],
                "gravity": 0, "turbulence": 0.35,
                "color_ramp": [{"position": 0, "color": [0.26, 0.85, 1.0, 1]}, {"position": 0.5, "color": [0.66, 0.55, 1.0, 0.8]}, {"position": 1, "color": [1.0, 0.42, 0.54, 0]}]
            }
        })
        for r_idx in range(4):
            radius = 180 + r_idx * 110
            layers.append(stroked_shape(
                f"vortex_ring_{r_idx}", width/2, height/2, PALETTE[r_idx], 3.0, (radius*2, radius*2),
                shape_type="ellipse",
                tracks=[
                    {"property": "rotation", "easing": "linear",
                     "keyframes": [{"frame": 0, "value": [0, 0, 0]}, {"frame": frames-1, "value": [0, 0, 360 * (1 if r_idx % 2 == 0 else -1)]}]},
                    {"property": "scale", "easing": "in_out_sine",
                     "keyframes": [{"frame": 0, "value": [0.7, 0.7]}, {"frame": frames//2, "value": [1.15, 1.15]}, {"frame": frames-1, "value": [0.85, 0.85]}]}
                ]
            ))

    elif slug == "shape_morphing":
        layers.append(shape("morph_circle", width/2, height/2, "#42D9FF", (260, 260), shape_type="ellipse", tracks=[
            {"property": "scale", "easing": "in_out_cubic",
             "keyframes": [{"frame": 0, "value": [1.0, 1.0]}, {"frame": 45, "value": [0.0, 0.0]}, {"frame": 135, "value": [0.0, 0.0]}, {"frame": frames-1, "value": [1.0, 1.0]}]},
            {"property": "opacity", "easing": "linear",
             "keyframes": [{"frame": 0, "value": 1.0}, {"frame": 45, "value": 0.0}, {"frame": 135, "value": 0.0}, {"frame": frames-1, "value": 1.0}]}
        ]))
        layers.append({"id": "morph_star", "type": "shape", "size": [280, 280], "position": [width/2, height/2],
            "enable_3d": True, "start_frame": 0, "duration_frames": frames,
            "shape": {"type": "star", "points": 5, "inner_radius_ratio": 0.45, "fill": rgb("#FFD166")},
            "animation": {"tracks": [
                {"property": "scale", "easing": "in_out_cubic",
                 "keyframes": [{"frame": 0, "value": [0.0, 0.0]}, {"frame": 45, "value": [1.1, 1.1]}, {"frame": 90, "value": [0.0, 0.0]}]},
                {"property": "rotation", "easing": "in_out_cubic",
                 "keyframes": [{"frame": 20, "value": [0, 0, -45]}, {"frame": 90, "value": [0, 0, 90]}]},
                {"property": "opacity", "easing": "linear",
                 "keyframes": [{"frame": 20, "value": 0.0}, {"frame": 45, "value": 1.0}, {"frame": 90, "value": 0.0}]}
            ]}
        })
        layers.append({"id": "morph_poly", "type": "shape", "size": [270, 270], "position": [width/2, height/2],
            "enable_3d": True, "start_frame": 0, "duration_frames": frames,
            "shape": {"type": "polygon", "points": 6, "fill": rgb("#FF6B8A")},
            "animation": {"tracks": [
                {"property": "scale", "easing": "in_out_cubic",
                 "keyframes": [{"frame": 75, "value": [0.0, 0.0]}, {"frame": 105, "value": [1.15, 1.15]}, {"frame": 140, "value": [0.0, 0.0]}]},
                {"property": "rotation", "easing": "in_out_cubic",
                 "keyframes": [{"frame": 75, "value": [0, 0, 0]}, {"frame": 140, "value": [0, 0, 120]}]},
                {"property": "opacity", "easing": "linear",
                 "keyframes": [{"frame": 75, "value": 0.0}, {"frame": 105, "value": 1.0}, {"frame": 140, "value": 0.0}]}
            ]}
        })

    elif slug == "puppet_pin_character":
        cx, cy = width/2, height/2 + 40
        layers.append(shape("rig_torso", cx, cy, "#52E0A0", (160, 220), shape_type="rect", tracks=[
            {"property": "rotation", "easing": "in_out_sine",
             "keyframes": [{"frame": 0, "value": [0, 0, 0]}, {"frame": 60, "value": [0, 0, -6]}, {"frame": 120, "value": [0, 0, 6]}, {"frame": frames-1, "value": [0, 0, 0]}]}
        ]))
        layers.append(shape("rig_head", cx, cy - 160, "#F4F7FF", (110, 110), shape_type="ellipse", tracks=[
            {"property": "position", "easing": "in_out_sine",
             "keyframes": [{"frame": 0, "value": [cx, cy-160]}, {"frame": 60, "value": [cx-8, cy-175]}, {"frame": 120, "value": [cx+8, cy-170]}, {"frame": frames-1, "value": [cx, cy-160]}]},
            {"property": "rotation", "easing": "in_out_sine",
             "keyframes": [{"frame": 0, "value": [0, 0, 0]}, {"frame": 60, "value": [0, 0, 12]}, {"frame": 120, "value": [0, 0, -10]}, {"frame": frames-1, "value": [0, 0, 0]}]}
        ]))
        layers.append(shape("rig_left_arm", cx - 110, cy - 40, "#42D9FF", (42, 140), shape_type="rect", tracks=[
            {"property": "rotation", "easing": "in_out_cubic",
             "keyframes": [{"frame": 0, "value": [0, 0, 15]}, {"frame": 80, "value": [0, 0, -65]}, {"frame": 160, "value": [0, 0, 25]}, {"frame": frames-1, "value": [0, 0, 15]}]}
        ]))
        layers.append(shape("rig_right_arm", cx + 110, cy - 40, "#42D9FF", (42, 140), shape_type="rect", tracks=[
            {"property": "rotation", "easing": "in_out_cubic",
             "keyframes": [{"frame": 0, "value": [0, 0, -15]}, {"frame": 80, "value": [0, 0, 75]}, {"frame": 160, "value": [0, 0, -30]}, {"frame": frames-1, "value": [0, 0, -15]}]}
        ]))

    elif slug == "full_body_ik_rig":
        cx, cy = width/2, height/2
        layers.append(shape("ik_pelvis", cx, cy, "#A98BFF", (120, 60), shape_type="rect", tracks=[
            {"property": "position", "easing": "in_out_sine",
             "keyframes": [{"frame": 0, "value": [cx, cy]}, {"frame": 60, "value": [cx, cy-20]}, {"frame": 120, "value": [cx, cy]}, {"frame": 180, "value": [cx, cy-20]}, {"frame": frames-1, "value": [cx, cy]}]}
        ]))
        layers.append(shape("ik_leg_l", cx - 45, cy + 100, "#42D9FF", (36, 150), shape_type="rect", tracks=[
            {"property": "rotation", "easing": "in_out_sine",
             "keyframes": [{"frame": 0, "value": [0, 0, -25]}, {"frame": 60, "value": [0, 0, 25]}, {"frame": 120, "value": [0, 0, -25]}, {"frame": 180, "value": [0, 0, 25]}, {"frame": frames-1, "value": [0, 0, -25]}]}
        ]))
        layers.append(shape("ik_leg_r", cx + 45, cy + 100, "#52E0A0", (36, 150), shape_type="rect", tracks=[
            {"property": "rotation", "easing": "in_out_sine",
             "keyframes": [{"frame": 0, "value": [0, 0, 25]}, {"frame": 60, "value": [0, 0, -25]}, {"frame": 120, "value": [0, 0, 25]}, {"frame": 180, "value": [0, 0, -25]}, {"frame": frames-1, "value": [0, 0, 25]}]}
        ]))
        layers.append(stroked_shape("ik_reach_target", cx + 260, cy - 80, "#FFD166", 4.0, (50, 50), shape_type="ellipse", tracks=[
            {"property": "scale", "easing": "in_out_sine", "keyframes": [{"frame": 0, "value": [0.8, 0.8]}, {"frame": 120, "value": [1.3, 1.3]}, {"frame": frames-1, "value": [0.8, 0.8]}]}
        ]))

    elif slug == "elastic_mesh_deformation":
        cx, cy = width/2, height/2
        layers.append(shape("elastic_logo", cx, cy, "#FF6B8A", (320, 220), shape_type="rect", tracks=[
            {"property": "scale", "easing": "in_out_cubic",
             "keyframes": [
                 {"frame": 0, "value": [1.0, 1.0]},
                 {"frame": 45, "value": [1.5, 0.7]},
                 {"frame": 90, "value": [0.8, 1.4]},
                 {"frame": 130, "value": [1.15, 0.95]},
                 {"frame": frames-1, "value": [1.0, 1.0]}
             ]},
            {"property": "rotation", "easing": "in_out_cubic",
             "keyframes": [{"frame": 0, "value": [0, 0, 0]}, {"frame": 50, "value": [0, 0, -18]}, {"frame": 90, "value": [0, 0, 22]}, {"frame": frames-1, "value": [0, 0, 0]}]}
        ]))
        for c_idx, (px, py) in enumerate([(-160, -110), (160, -110), (160, 110), (-160, 110)]):
            layers.append(shape(f"pin_{c_idx}", cx + px, cy + py, "#FFD166", (24, 24), shape_type="ellipse"))

    elif slug == "animated_rotoscope_mask":
        for g_idx in range(6):
            layers.append(stroked_shape(f"bg_stripe_{g_idx}", width/2, 200 + g_idx * 140, "#1F2B48", 3.0, (1800, 2), shape_type="rect"))
        cx, cy = width/2, height/2 + 30
        layers.append(shape("roto_torso", cx, cy, "#F4F7FF", (190, 260), shape_type="rect", tracks=[
            {"property": "position", "easing": "in_out_sine",
             "keyframes": [{"frame": 0, "value": [cx, cy]}, {"frame": 80, "value": [cx-50, cy-15]}, {"frame": 160, "value": [cx+50, cy-10]}, {"frame": frames-1, "value": [cx, cy]}]}
        ]))
        layers.append(shape("roto_head", cx, cy - 180, "#42D9FF", (120, 120), shape_type="ellipse", tracks=[
            {"property": "position", "easing": "in_out_sine",
             "keyframes": [{"frame": 0, "value": [cx, cy-180]}, {"frame": 80, "value": [cx-65, cy-195]}, {"frame": 160, "value": [cx+65, cy-190]}, {"frame": frames-1, "value": [cx, cy-180]}]}
        ]))

    elif slug == "liquid_mask_reveal":
        cx, cy = width/2, height/2
        layers.append(shape("underlying_plate", cx, cy, "#A98BFF", (640, 420), shape_type="rect"))
        layers.append(text("revealed_text", "CHRONON REVEAL", cx - 250, cy + 15, 42, "#F4F7FF"))
        layers.append(shape("liquid_blob", cx, cy, "#080D18", (900, 600), shape_type="ellipse", tracks=[
            {"property": "scale", "easing": "in_out_cubic",
             "keyframes": [{"frame": 0, "value": [1.4, 1.4]}, {"frame": frames//2, "value": [0.6, 0.7]}, {"frame": frames-1, "value": [0.0, 0.0]}]},
            {"property": "rotation", "easing": "linear",
             "keyframes": [{"frame": 0, "value": [0, 0, 0]}, {"frame": frames-1, "value": [0, 0, 90]}]}
        ]))

    elif slug == "cinematic_parallax_scene":
        z_depths = [0, 350, 750, 1200, 1800, 2500]
        for i, z_val in enumerate(z_depths):
            x_pos = 280 + i * 270
            y_pos = 720 - i * 65
            w = 260 - i * 20
            h = 160 - i * 14
            layers.append(shape(
                f"parallax_layer_{i}", x_pos, y_pos, PALETTE[i % 6], (w, h),
                shape_type="rect", z=float(z_val),
                tracks=[
                    {"property": "position", "easing": "in_out_sine",
                     "keyframes": [{"frame": 0, "value": [x_pos - (6 - i)*35, y_pos]}, {"frame": frames-1, "value": [x_pos + (6 - i)*35, y_pos]}]}
                ]
            ))

    elif slug == "floating_product_orbit":
        cx, cy = width/2, height/2
        layers.append(shape("hero_product", cx, cy, "#F4F7FF", (220, 220), shape_type="rect"))
        for i in range(6):
            phase = i * (2 * math.pi / 6)
            p_keys = []
            s_keys = []
            for f_idx in [0, frames//4, frames//2, 3*frames//4, frames-1]:
                t = f_idx / (frames - 1)
                angle = phase + 2 * math.pi * t
                ox = cx + 420 * math.cos(angle)
                oy = cy + 160 * math.sin(angle)
                scale_val = 0.75 + 0.35 * math.sin(angle)
                p_keys.append({"frame": f_idx, "value": [round(ox, 1), round(oy, 1)]})
                s_keys.append({"frame": f_idx, "value": [round(scale_val, 2), round(scale_val, 2)]})
            layers.append(shape(
                f"orbit_card_{i}", cx, cy, PALETTE[i % 6], (120, 120), shape_type="rect",
                tracks=[
                    {"property": "position", "easing": "linear", "keyframes": p_keys},
                    {"property": "scale", "easing": "linear", "keyframes": s_keys}
                ]
            ))

    elif slug == "real_3d_geometry_test":
        cx, cy = width/2, height/2
        for layer_idx in range(3):
            size_val = 260 - layer_idx * 60
            layers.append(stroked_shape(
                f"geo_ring_{layer_idx}", cx, cy, PALETTE[layer_idx], 4.0, (size_val, size_val),
                shape_type="rect", z=float(layer_idx * 150),
                tracks=[
                    {"property": "rotation", "easing": "linear",
                     "keyframes": [{"frame": 0, "value": [0, 0, 0]}, {"frame": frames-1, "value": [0, 0, 360 * (1 if layer_idx % 2 == 0 else -1)]}]},
                    {"property": "scale", "easing": "in_out_sine",
                     "keyframes": [{"frame": 0, "value": [1.0, 1.0]}, {"frame": frames//2, "value": [1.2, 0.85]}, {"frame": frames-1, "value": [1.0, 1.0]}]}
                ]
            ))
        layers.append(shape("geo_core", cx, cy, "#F4F7FF", (90, 90), shape_type="ellipse"))

    elif slug == "advanced_glitch_transition":
        cx, cy = width/2, height/2
        layers.append(shape("plate_a", cx, cy, "#42D9FF", (700, 420), shape_type="rect", tracks=[
            {"property": "opacity", "easing": "linear", "keyframes": [{"frame": 0, "value": 1.0}, {"frame": 70, "value": 1.0}, {"frame": 75, "value": 0.0}]}
        ]))
        layers.append(shape("glitch_red", cx - 25, cy, "#FF3366", (700, 420), shape_type="rect", blend_mode="add", tracks=[
            {"property": "opacity", "easing": "linear", "keyframes": [{"frame": 60, "value": 0.0}, {"frame": 70, "value": 0.75}, {"frame": 85, "value": 0.0}]}
        ]))
        layers.append(shape("glitch_cyan", cx + 25, cy, "#00FFFF", (700, 420), shape_type="rect", blend_mode="add", tracks=[
            {"property": "opacity", "easing": "linear", "keyframes": [{"frame": 60, "value": 0.0}, {"frame": 70, "value": 0.75}, {"frame": 85, "value": 0.0}]}
        ]))
        for s_idx in range(6):
            sy = 300 + s_idx * 80
            layers.append(shape(f"slice_{s_idx}", cx, sy, "#F4F7FF", (800, 16), shape_type="rect", tracks=[
                {"property": "position", "easing": "linear",
                 "keyframes": [{"frame": 65, "value": [cx, sy]}, {"frame": 72, "value": [cx + (s_idx%2 * 2 - 1)*90, sy]}, {"frame": 82, "value": [cx, sy]}]},
                {"property": "opacity", "easing": "linear",
                 "keyframes": [{"frame": 64, "value": 0.0}, {"frame": 70, "value": 0.9}, {"frame": 84, "value": 0.0}]}
            ]))
        layers.append(shape("plate_b", cx, cy, "#FFD166", (700, 420), shape_type="rect", tracks=[
            {"property": "opacity", "easing": "linear", "keyframes": [{"frame": 74, "value": 0.0}, {"frame": 80, "value": 1.0}, {"frame": frames-1, "value": 1.0}]}
        ]))

    elif slug == "cinematic_glow_light_rays":
        layers.append(shape("light_source", 200, 540, "#F4F7FF", (60, 60), shape_type="ellipse", tracks=[
            {"property": "position", "easing": "in_out_cubic",
             "keyframes": [{"frame": 0, "value": [200, 540]}, {"frame": frames-1, "value": [1720, 540]}]}
        ]))
        layers.append(shape("bloom_halo", 200, 540, "#42D9FF", (280, 280), shape_type="ellipse", blend_mode="add", tracks=[
            {"property": "position", "easing": "in_out_cubic",
             "keyframes": [{"frame": 0, "value": [200, 540]}, {"frame": frames-1, "value": [1720, 540]}]},
            {"property": "scale", "easing": "in_out_sine",
             "keyframes": [{"frame": 0, "value": [0.8, 0.8]}, {"frame": frames//2, "value": [1.4, 1.4]}, {"frame": frames-1, "value": [0.9, 0.9]}]}
        ]))
        for ray_idx in range(5):
            ray_angle = -40 + ray_idx * 20
            layers.append(shape(f"light_ray_{ray_idx}", 960, 540, "#A98BFF", (800, 24), shape_type="rect", rotation=ray_angle, blend_mode="add", tracks=[
                {"property": "opacity", "easing": "in_out_sine",
                 "keyframes": [{"frame": 0, "value": 0.15}, {"frame": frames//2, "value": 0.65}, {"frame": frames-1, "value": 0.2}]}
            ]))

    elif slug == "velocity_motion_blur":
        layers.append(text("velocity_title", "HYPER VELOCITY", -600, 480, 72, "#42D9FF", tracks=[
            {"property": "position", "easing": "in_out_cubic",
             "keyframes": [{"frame": 0, "value": [-600, 480]}, {"frame": 45, "value": [960, 480]}, {"frame": 105, "value": [960, 480]}, {"frame": frames-1, "value": [2500, 480]}]}
        ]))
        for b_idx in range(4):
            by = 380 + b_idx * 90
            layers.append(shape(f"fast_bar_{b_idx}", -800, by, PALETTE[b_idx], (280, 22), shape_type="rect", tracks=[
                {"property": "position", "easing": "in_out_quad",
                 "keyframes": [{"frame": b_idx * 8, "value": [-800, by]}, {"frame": frames - 20 + b_idx * 5, "value": [2600, by]}]}
            ]))

    elif slug == "kinetic_typography":
        layers.append(text("word_0", "MAKE", 250, 440, 76, PALETTE[0], tracks=[
            {"property": "position", "easing": "out_cubic", "keyframes": [{"frame": 0, "value": [250, 650]}, {"frame": 35, "value": [250, 440]}]},
            {"property": "opacity", "easing": "out_cubic", "keyframes": [{"frame": 0, "value": 0.0}, {"frame": 30, "value": 1.0}]}
        ]))
        layers.append(text("word_1", "IDEAS", 620, 440, 76, PALETTE[1], tracks=[
            {"property": "rotation", "easing": "out_cubic", "keyframes": [{"frame": 25, "value": [0, 0, -60]}, {"frame": 60, "value": [0, 0, 0]}]},
            {"property": "opacity", "easing": "linear", "keyframes": [{"frame": 25, "value": 0.0}, {"frame": 45, "value": 1.0}]}
        ]))
        layers.append(text("word_2", "MOVE", 980, 440, 76, PALETTE[2], tracks=[
            {"property": "scale", "easing": "out_cubic",
             "keyframes": [{"frame": 50, "value": [0.0, 0.0]}, {"frame": 85, "value": [1.0, 1.0]}]},
            {"property": "opacity", "easing": "linear", "keyframes": [{"frame": 50, "value": 0.0}, {"frame": 70, "value": 1.0}]}
        ]))
        layers.append(text("word_3", "WITH", 480, 580, 76, PALETTE[3], tracks=[
            {"property": "position", "easing": "out_cubic", "keyframes": [{"frame": 75, "value": [300, 580]}, {"frame": 110, "value": [480, 580]}]},
            {"property": "opacity", "easing": "linear", "keyframes": [{"frame": 75, "value": 0.0}, {"frame": 100, "value": 1.0}]}
        ]))
        layers.append(text("word_4", "CHRONON", 800, 580, 76, PALETTE[4], tracks=[
            {"property": "scale", "easing": "out_cubic", "keyframes": [{"frame": 100, "value": [0.5, 0.5]}, {"frame": 135, "value": [1.0, 1.0]}]},
            {"property": "opacity", "easing": "linear", "keyframes": [{"frame": 100, "value": 0.0}, {"frame": 125, "value": 1.0}]}
        ]))

    elif slug == "documentary_map_flyover":
        for gl in range(5):
            layers.append(stroked_shape(f"map_grid_h_{gl}", width/2, 250 + gl * 140, "#19253B", 2.0, (1700, 2), shape_type="rect"))
        layers.append({
            "id": "flight_route", "type": "shape", "size": [width, height],
            "position": [width/2, height/2], "enable_3d": True, "start_frame": 0, "duration_frames": frames,
            "shape": {
                "type": "path",
                "path": [
                    {"type": "move_to", "point": [-600, 100]},
                    {"type": "cubic_to", "control1": [-200, -220], "control2": [150, 240], "point": [600, -80]}
                ],
                "stroke": {"color": "#42D9FF", "width": 8, "dash_array": [2200, 2200], "dash_offset": 2200}
            },
            "animation": {
                "tracks": [
                    {"property": "stroke_dash_offset", "easing": "in_out_cubic",
                     "keyframes": [{"frame": 20, "value": 2200}, {"frame": 180, "value": 0}]}
                ]
            }
        })
        waypoints = [("Paris", 360, 640, 20), ("Milan", 960, 500, 90), ("Rome", 1560, 460, 160)]
        for w_name, wx, wy, wf in waypoints:
            layers.append(shape(f"pin_outer_{w_name}", wx, wy, "#FF6B8A", (34, 34), shape_type="ellipse", tracks=[
                {"property": "scale", "easing": "out_cubic", "keyframes": [{"frame": wf, "value": [0.0, 0.0]}, {"frame": wf+20, "value": [1.0, 1.0]}]},
                {"property": "opacity", "easing": "linear", "keyframes": [{"frame": wf, "value": 0.0}, {"frame": wf+15, "value": 1.0}]}
            ]))
            layers.append(shape(f"pin_core_{w_name}", wx, wy, "#F4F7FF", (14, 14), shape_type="ellipse", tracks=[
                {"property": "opacity", "easing": "linear", "keyframes": [{"frame": wf, "value": 0.0}, {"frame": wf+15, "value": 1.0}]}
            ]))
            layers.append(text(f"label_{w_name}", w_name.upper(), wx - 60, wy + 40, 26, "#F4F7FF", tracks=[
                {"property": "opacity", "easing": "linear", "keyframes": [{"frame": wf+10, "value": 0.0}, {"frame": wf+30, "value": 1.0}]}
            ]))

    elif slug == "product_explainer_ultimate":
        cx, cy = width/2, height/2 + 20
        layers.append(shape("presenter_card", cx, cy, "#52E0A0", (220, 320), shape_type="rect", tracks=[
            {"property": "position", "easing": "in_out_sine",
             "keyframes": [{"frame": 0, "value": [cx, cy]}, {"frame": 150, "value": [cx, cy-15]}, {"frame": frames-1, "value": [cx, cy]}]}
        ]))
        p_offsets = [(-460, -200), (460, -200), (-460, 220), (460, 220)]
        for p_idx, (px, py) in enumerate(p_offsets):
            start_f = 20 + p_idx * 15
            layers.append(shape(f"product_card_{p_idx}", cx + px, cy + py, PALETTE[p_idx], (200, 140), shape_type="rect", tracks=[
                {"property": "position", "easing": "out_cubic",
                 "keyframes": [{"frame": start_f, "value": [cx + px*1.8, cy + py*1.8]}, {"frame": start_f + 35, "value": [cx + px, cy + py]}]},
                {"property": "opacity", "easing": "linear",
                 "keyframes": [{"frame": start_f, "value": 0.0}, {"frame": start_f + 25, "value": 1.0}]}
            ]))
        layers.append(text("explainer_headline", "ALL-IN-ONE MOTION GRAPHICS", 380, 140, 48, "#F4F7FF", tracks=[
            {"property": "opacity", "easing": "out_cubic", "keyframes": [{"frame": 10, "value": 0.0}, {"frame": 40, "value": 1.0}]}
        ]))

    elif slug == "cinematic_documentary_montage":
        cx, cy = width/2, height/2
        layers.append(shape("archive_plate", cx, cy, "#162238", (1000, 600), shape_type="rect", tracks=[
            {"property": "scale", "easing": "linear", "keyframes": [{"frame": 0, "value": [1.0, 1.0]}, {"frame": frames-1, "value": [1.12, 1.12]}]}
        ]))
        layers.append(shape("isolated_subject", cx - 80, cy + 30, "#FF6B8A", (260, 360), shape_type="rect", tracks=[
            {"property": "scale", "easing": "out_cubic", "keyframes": [{"frame": 60, "value": [1.0, 1.0]}, {"frame": 90, "value": [1.2, 1.2]}]}
        ]))
        layers.append(text("doc_subtitle", "CHRONON HISTORICAL ARCHIVE — 1924", 200, 930, 28, "#F4F7FF", tracks=[
            {"property": "opacity", "easing": "out_cubic", "keyframes": [{"frame": 130, "value": 0.0}, {"frame": 160, "value": 1.0}]}
        ]))

    elif slug == "chronon_master_composition":
        cx, cy = width/2, height/2
        layers.append({
            "id": "master_particles", "type": "particles", "size": [width, height], "position": [cx, cy],
            "duration_frames": frames,
            "particle_emitter": {
                "count": 200, "seed": 20261012, "emitter_size": [800, 800],
                "velocity_min": [-4, -4], "velocity_max": [4, 4], "lifetime": [3, 7], "size": [2, 10],
                "gravity": 0, "turbulence": 0.25,
                "color_ramp": [{"position": 0, "color": [0.26, 0.85, 1.0, 0.8]}, {"position": 1, "color": [0.66, 0.35, 1.0, 0]}]
            }
        })
        layers.append({
            "id": "master_route", "type": "shape", "size": [width, height], "position": [cx, cy],
            "enable_3d": True, "start_frame": 0, "duration_frames": frames,
            "shape": {
                "type": "path",
                "path": [
                    {"type": "move_to", "point": [-800, 180]},
                    {"type": "cubic_to", "control1": [-280, -320], "control2": [280, 320], "point": [800, -180]}
                ],
                "stroke": {"color": "#42D9FF", "width": 8, "dash_array": [2400, 2400], "dash_offset": 2400}
            },
            "animation": {"tracks": [
                {"property": "stroke_dash_offset", "easing": "in_out_cubic", "keyframes": [{"frame": 0, "value": 2400}, {"frame": frames-1, "value": 0}]}
            ]}
        })
        layers.append(stroked_shape("master_core_ring", cx, cy, "#FFD166", 4.0, (260, 260), shape_type="rect", tracks=[
            {"property": "rotation", "easing": "linear", "keyframes": [{"frame": 0, "value": [0, 0, 0]}, {"frame": frames-1, "value": [0, 0, 360]}]},
            {"property": "scale", "easing": "in_out_sine", "keyframes": [{"frame": 0, "value": [1.0, 1.0]}, {"frame": 150, "value": [1.25, 1.25]}, {"frame": frames-1, "value": [1.0, 1.0]}]}
        ]))
        layers.append(shape("master_core", cx, cy, "#FF6B8A", (100, 100), shape_type="ellipse"))
        for s_idx in range(4):
            angle = s_idx * (math.pi / 2)
            layers.append(shape(f"master_satellite_{s_idx}", cx + 380*math.cos(angle), cy + 200*math.sin(angle), PALETTE[s_idx], (70, 70), shape_type="rect"))
        layers.append(text("master_title", "CHRONON ENGINE COMPLETE", 360, 140, 52, "#F4F7FF", tracks=[
            {"property": "opacity", "easing": "out_cubic", "keyframes": [{"frame": 15, "value": 0.0}, {"frame": 50, "value": 1.0}]}
        ]))

    for i in range(3):
        layers.append(shape(f"bottom_rule_{i}", 110+i*45, 1010, PALETTE[i], (28, 8)))
    return {"schema": "chronon.render-plan.v3", "version": 3, "job_id": f"showcase_{ident}_{slug}",
            "canvas": {"width": width, "height": height, "fps_num": fps, "fps_den": 1, "duration_frames": frames},
            "layers": layers, "output": {"path": str(output), "format": "mp4", "codec": "h264"}}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli",type=pathlib.Path,default=CLI_DEFAULT)
    parser.add_argument("--output-root",type=pathlib.Path,default=HERE/"artifacts")
    parser.add_argument("--case",action="append")
    parser.add_argument("--list",action="store_true")
    parser.add_argument("--validate-only",action="store_true")
    parser.add_argument("--render-supported",action="store_true")
    parser.add_argument("--render-partial",action="store_true")
    parser.add_argument("--repeat",action="store_true")
    parser.add_argument("--backend",choices=["software","vulkan"],default="software")
    args=parser.parse_args()
    manifest=json.loads(MANIFEST_PATH.read_text(encoding="utf-8")); cases=manifest["cases"]
    if args.list:
        for c in cases: print(f"{c['id']} {c['slug']}: {c['duration_seconds']}s {c['status']} — {c['name']}")
        return 0
    cli=args.cli.resolve()
    if not cli.is_file(): parser.error(f"Chronon3D CLI not found: {cli}; pass --cli")
    selected=cases
    if args.case:
        selected=[c for c in cases if c["id"] in args.case or c["slug"] in args.case]
        found={v for c in selected for v in (c["id"],c["slug"])}
        if set(args.case)-found: parser.error("unknown cases: "+", ".join(sorted(set(args.case)-found)))
    if args.render_supported: selected=[c for c in selected if c["status"]!="BLOCKED"]
    elif not args.validate_only and not args.render_partial: parser.error("choose --validate-only, --render-supported, or --render-partial")
    root=args.output_root.resolve(); root.mkdir(parents=True,exist_ok=True); logs=root/"logs"
    report={"suite_id":manifest["suite_id"],"manifest_sha256":digest(MANIFEST_PATH),"renderer":str(cli),
        "backend":args.backend,"environment":{"python":sys.version.split()[0],"platform":sys.platform,
        "ffmpeg":shutil.which("ffmpeg"),"ffprobe":shutil.which("ffprobe")},"cases":[]}
    for case in selected:
        folder=root/f"{case['id']}_{case['slug']}"; folder.mkdir(parents=True,exist_ok=True)
        n=case["duration_seconds"]*30; video=folder/"output.mp4"; plan=folder/"scene.json"
        plan.write_text(json.dumps(scene(case,video),indent=2)+"\n",encoding="utf-8")
        expected={"case_id":case["id"],"slug":case["slug"],"duration_seconds":case["duration_seconds"],
            "width":1920,"height":1080,"fps_num":30,"fps_den":1,"frame_count":n,
            "requested_status":case["status"],"implemented":case["implemented"],"blocked_features":case["blocked"]}
        (folder/"expected.json").write_text(json.dumps(expected,indent=2)+"\n",encoding="utf-8")
        result={**expected,"scene_sha256":digest(plan),"status":case["status"],
            "technical_status":"NOT_RUN","functional_pass":False,"artifacts":{"scene":str(plan),"expected":str(folder/"expected.json")}}
        started=time.monotonic()
        try:
            vlog=logs/f"{case['id']}_validate.log"
            validation=run([str(cli),"validate","--plan",str(plan),"--assets-root",str((REPO/"Chronon3d").resolve())],log=vlog)
            json_start=validation.rfind("\n{")
            val=json.loads(validation[json_start+1:] if json_start >= 0 else validation[validation.find("{"):])
            result["artifacts"]["validation_log"]=str(vlog)
            if val.get("status")!="PASS" or val.get("valid") is not True: raise RuntimeError(f"plan validation failed: {val}")
            expected["validated_plan_sha256"]=digest(plan)
            (folder/"expected.json").write_text(json.dumps(expected,indent=2)+"\n",encoding="utf-8")
            result.update(expected)
            if not args.validate_only and case["status"]!="BLOCKED":
                log=logs/f"{case['id']}_render.log"
                hw = "auto" if args.backend == "vulkan" else "none"
                enc = "pipe" if args.backend == "vulkan" else "native"
                run([str(cli),"render","--plan",str(plan),"--backend",args.backend,"--profile","preview",
                     "--hardware",hw,"--fps","30","--encoder-backend",enc,"--assets-root",str((REPO/"Chronon3d").resolve()),"-o",str(video)],log=log)
                probe_out=run(["ffprobe","-v","error","-select_streams","v:0","-show_entries",
                    "stream=codec_name,width,height,pix_fmt,r_frame_rate,nb_frames,duration,time_base",
                    "-show_entries","format=duration","-of","json",str(video)])
                probe=json.loads(probe_out); stream=probe["streams"][0]
                if (stream.get("codec_name")!="h264" or int(stream["width"])!=1920 or int(stream["height"])!=1080
                    or stream["r_frame_rate"]!="30/1" or int(stream.get("nb_frames",-1))!=n):
                    raise RuntimeError(f"stream contract mismatch: {stream}")
                frames_dir=folder/"frames"; frames_dir.mkdir(exist_ok=True)
                hashes_out=run(["ffmpeg","-v","error","-i",str(video),"-f","framemd5","-"])
                hashes=[line.rsplit(",",1)[-1].strip() for line in hashes_out.splitlines() if line.strip() and not line.startswith("#")]
                if len(hashes)!=n or len(set(hashes))<2: raise RuntimeError(f"decoded frame count/change check failed: {len(hashes)}")
                if any(hashes[index] == hashes[index+1] for index in range(len(hashes)-1)):
                    # Repeated hashes are evidence, not automatic failure: sparse rendered changes may legitimately hold frames.
                    pass
                for idx in sorted(set((0,n//2,n-1))):
                    target=frames_dir/f"frame_{idx:03}.png"
                    run(["ffmpeg","-v","error","-y","-i",str(video),"-vf",f"select=eq(n\\,{idx})","-vsync","0","-frames:v","1",str(target)])
                    if not target.is_file() or target.stat().st_size==0: raise RuntimeError(f"missing frame {idx}")
                repeated=None
                if args.repeat:
                    repeat=folder/"repeat.mp4"
                    run([str(cli),"render","--plan",str(plan),"--backend",args.backend,"--profile","preview",
                         "--hardware","none","--fps","30","--encoder-backend","native","--assets-root",str(REPO/"Chronon3d"),"-o",str(repeat)],log=logs/f"{case['id']}_repeat.log")
                    rh=run(["ffmpeg","-v","error","-i",str(repeat),"-f","framemd5","-"])
                    rh=[line.rsplit(",",1)[-1].strip() for line in rh.splitlines() if line.strip() and not line.startswith("#")]
                    repeated=hashes==rh
                timing_path=pathlib.Path(str(video)+".timing.json")
                timing=json.loads(timing_path.read_text()) if timing_path.is_file() else {}
                def finite_numbers(value):
                    if isinstance(value, dict):
                        for child in value.values(): yield from finite_numbers(child)
                    elif isinstance(value, list):
                        for child in value: yield from finite_numbers(child)
                    elif isinstance(value, (int, float)) and not isinstance(value, bool):
                        yield float(value)
                nums=list(finite_numbers(timing))
                if any(not math.isfinite(value) for value in nums): raise RuntimeError("non-finite timing metric")
                if int(probe["streams"][0].get("nb_frames", -1)) != n or abs(float(probe["format"]["duration"])-case["duration_seconds"]) > 1/30000:
                    raise RuntimeError(f"exact duration/frame contract failed: {probe}")
                frame_coverage=[]
                for idx in sorted(set((0,n//2,n-1))):
                    target=frames_dir/f"frame_{idx:03}.png"
                    if not target.is_file() or target.stat().st_size==0: raise RuntimeError(f"missing frame {idx}")
                    stats=run(["ffmpeg","-v","info","-i",str(target),"-vf","signalstats,metadata=print","-frames:v","1","-f","null","-"])
                    import re
                    values={key:float(value) for key,value in re.findall(r"lavfi\.signalstats\.(YAVG|UAVG|VAVG|YMAX|YMIN)=([0-9.]+)",stats)}
                    if not {"YAVG","UAVG","VAVG"}.issubset(values) or values["YAVG"] <= 0:
                        raise RuntimeError(f"representative frame has no finite measurable pixel coverage: {idx}: {values}")
                    frame_coverage.append({"frame":idx,"bytes":target.stat().st_size,"signalstats":values})
                timing_ms=timing.get("frame_times_ms", [])
                if not isinstance(timing_ms, list) or len(timing_ms) != n:
                    raise RuntimeError(f"timing sidecar has no exact per-frame samples: {len(timing_ms) if isinstance(timing_ms,list) else 'missing'} vs {n}")
                result["representative_frames"]=frame_coverage
                result["timing_values_checked"]=len(nums)
                result.update({"status":"PARTIAL" if case["status"]!="PASS" else "PASS",
                    "technical_status":"PASS","functional_pass":False,"scope_note":"Video is a deterministic diagnostic fixture, not a visual golden or full functional certification.","probe":stream,
                    "decoded_frame_sha256":hashlib.sha256("\n".join(hashes).encode()).hexdigest(),
                    "distinct_decoded_frames":len(set(hashes)),"render_wall_seconds":round(time.monotonic()-started,6),
                    "peak_rss_kib_process":resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss,
                    "repeat_decoded_hashes_equal":repeated,"artifacts":{**result["artifacts"],"video":str(video),"metrics":str(folder/"metrics.json"),
                    "timing":str(timing_path),"frames_dir":str(frames_dir),"probe":str(folder/"probe.json"),"render_log":str(log)}})
                (folder/"probe.json").write_text(json.dumps(probe,indent=2)+"\n")
                result["expected_sha256"]=digest(folder/"expected.json")
                metric_fields=("case_id","slug","scene_sha256","probe","decoded_frame_sha256","distinct_decoded_frames","render_wall_seconds","peak_rss_kib_process","timing_values_checked","representative_frames","repeat_decoded_hashes_equal","technical_status","functional_pass","scope_note")
                (folder/"metrics.json").write_text(json.dumps({key:result.get(key) for key in metric_fields},indent=2)+"\n",encoding="utf-8")
            result["artifacts"]["metrics"]=str(folder/"metrics.json")
            (folder/"expected.json").write_text(json.dumps(expected,indent=2)+"\n",encoding="utf-8")
            result["expected_sha256"]=digest(folder/"expected.json")
        except Exception as exc:
            result.update({"status":"FAIL","technical_status":"FAIL","error":str(exc)})
        if result["technical_status"] == "NOT_RUN":
            result["functional_status"] = "BLOCKED" if case["status"] == "BLOCKED" else "NOT_CERTIFIED"
        report["cases"].append(result); print(f"{result['status']:7} {case['id']} {case['slug']}: {result['technical_status']}")
    report["summary"]={s:sum(c["status"]==s for c in report["cases"]) for s in ("PASS","PARTIAL","BLOCKED","FAIL")}
    report_path=root/"report.json"; report_path.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(f"report: {report_path} {report['summary']}")
    return int(report["summary"]["FAIL"]>0)

if __name__=="__main__": raise SystemExit(main())

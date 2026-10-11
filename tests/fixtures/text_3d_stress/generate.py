#!/usr/bin/env python3
"""Generate render-plan stress cases from the capabilities exposed by Chronon3D."""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "plans"
OUT.mkdir(parents=True, exist_ok=True)
W, H, N = 960, 540, 72
FONT = "assets/fonts/Inter-Bold.ttf"


def kf(values, easing="in_out_cubic"):
    return [{"frame": f, "value": v} for f, v in values]


def track(prop, values, easing="in_out_cubic"):
    return {"property": prop, "easing": easing, "keyframes": kf(values)}


def animator(unit, props, selector_order="forward", shape="ramp_up", start=None, end=None):
    selector = {"unit": unit, "shape": shape, "order": selector_order,
                "combine": "replace", "exclude_spaces": True}
    if start is not None:
        selector["start"] = {"keyframes": kf(start), "easing": "linear"}
    if end is not None:
        selector["end"] = {"keyframes": kf(end), "easing": "linear"}
    return {"selectors": [selector], "properties": [sampled_text_track(p) for p in props]}


def prop_track(prop, values, easing="in_out_cubic"):
    return track(prop, values, easing)


def easing_value(name, t):
    if name == "linear":
        return t
    if name == "in_out_cubic":
        return 4 * t * t * t if t < 0.5 else 1 - ((-2 * t + 2) ** 3) / 2
    if name == "out_cubic":
        return 1 - (1 - t) ** 3
    if name == "out_back":
        c1 = 1.70158
        c3 = c1 + 1
        return 1 + c3 * (t - 1) ** 3 + c1 * (t - 1) ** 2
    raise ValueError(f"unsupported text animator easing: {name}")


def sampled_text_track(track_spec):
    """Bake a sparse eased track to the decoder's sampled-linear contract."""
    keys = track_spec["keyframes"]
    easing = track_spec.get("easing", "linear")
    first_frame = keys[0]["frame"]
    last_frame = keys[-1]["frame"]
    values = []
    for frame in range(first_frame, last_frame + 1):
        segment = next((i for i in range(len(keys) - 1)
                        if keys[i]["frame"] <= frame <= keys[i + 1]["frame"]), None)
        if segment is None:
            value = keys[0]["value"] if frame < first_frame else keys[-1]["value"]
        else:
            a, b = keys[segment], keys[segment + 1]
            span = b["frame"] - a["frame"]
            t = 1.0 if span <= 0 else (frame - a["frame"]) / span
            t = easing_value(easing, t)
            av, bv = a["value"], b["value"]
            if isinstance(av, list):
                value = [x + (y - x) * t for x, y in zip(av, bv)]
            else:
                value = av + (bv - av) * t
        values.append({"frame": frame, "value": value})
    return {"property": track_spec["property"], "easing": "linear", "keyframes": values}


def make_layer(text="MOTION", *, enable_3d=False, position=(480, 270), size=(860, 300), font_size=148,
               depth=0, animation=None, animators=None):
    layer = {"id": "title", "type": "text", "text": text, "size": list(size),
             "position": list(position), "depth_offset": depth, "enable_3d": enable_3d,
             "style": {"font": FONT, "font_size": font_size, "fill": "#F4F5F7"},
             "start_frame": 0, "duration_frames": N}
    if animation:
        layer["animation"] = {"tracks": animation}
    if animators:
        layer["text_animators"] = animators
    return layer


def camera(position=(0, 0, -1000), rotation=(0, 0, 0), fov=45, near=0.1, far=10000):
    return {"type": "perspective", "position": list(position), "rotation_deg": list(rotation),
            "fov_deg": fov, "near": near, "far": far}


def plan(name, layers, *, cam=None, cam_tracks=None):
    for index, layer in enumerate(layers, start=1):
        layer["id"] = f"layer_{index}"
    p = {"schema": "chronon.render-plan.v2", "version": 2, "job_id": "text3d_stress_" + name,
         "canvas": {"width": W, "height": H, "fps_num": 24, "fps_den": 1, "duration_frames": N},
         "layers": [{"id": "bg", "type": "color", "color": [0, 0, 0, 1],
                     "size": [W, H], "start_frame": 0, "duration_frames": N}, *layers],
         "output": {"path": name + ".mp4", "format": "mp4", "codec": "h264"}}
    if cam:
        p["camera"] = cam
    if cam_tracks:
        p["camera_animation"] = {"tracks": cam_tracks}
    (OUT / (name + ".plan.json")).write_text(json.dumps(p, indent=2) + "\n")


# Per-glyph rotation is currently absent: scale collapse tests selector ordering and batching only.
flip = animator("glyph", [prop_track("scale_y", [(0, 1), (22, 0.03), (48, 1), (71, 1)])],
                start=[(0, 0), (71, 0)], end=[(0, 0), (50, 100), (71, 100)])
plan("01_wave_cascade_flip", [make_layer(animators=[flip])])

# Randomized per-glyph XY scatter/reassembly; the renderer's selector uses deterministic random order.
explosion = animator("glyph", [prop_track("position", [(0, [105, -85]), (35, [0, 0]), (71, [0, 0])], "out_back")],
                     selector_order="random", shape="smooth",
                     start=[(0, 0), (71, 0)], end=[(0, 100), (22, 100), (58, 0), (71, 0)])
plan("02_explosion_reassembly", [make_layer(animators=[explosion])])

# 2D per-glyph corkscrew proxy: staggered diagonal path + tracking; no animated glyph Z/rotation-Z API.
twist = animator("glyph", [prop_track("position", [(0, [0, 130]), (36, [0, 0]), (71, [0, 0])]),
                            prop_track("tracking", [(0, 18), (36, 0), (71, 0)])],
                 start=[(0, 0), (71, 0)], end=[(0, 100), (45, 100), (71, 100)])
plan("03_per_glyph_twist_proxy", [make_layer(animators=[twist])])

# Text-run displacement across glyphs: evaluates selector and transformed glyph batching, not a GPU mesh shader.
ripple = animator("glyph", [prop_track("position_y", [(0, 45), (18, -45), (36, 45), (54, -45), (71, 0)], "linear")],
                  shape="triangle", start=[(0, 0), (71, 0)], end=[(0, 100), (71, 100)])
plan("04_sine_ripple_glyph_proxy", [make_layer(animators=[ripple])])

# Line-level scale fold tests hierarchy/line selectors; no geometric sheet bend.
page = animator("line", [prop_track("scale_x", [(0, 0.02), (30, 1), (71, 1)], "out_cubic"),
                          prop_track("opacity", [(0, 0), (25, 1), (71, 1)])],
                start=[(0, 0), (71, 0)], end=[(0, 100), (71, 100)])
plan("05_page_flip_line_proxy", [make_layer(text="PAGE\nTURN", animators=[page])])

# Camera dolly approaches and crosses the text plane; exercises near clip/depth behavior.
plan("06_camera_fly_through", [make_layer(text="O", enable_3d=True, font_size=360, size=(500, 440))],
     cam=camera(), cam_tracks=[track("camera_position_z", [(0, -1150), (30, -160), (42, -0.2), (55, 180), (71, -1100)])])

# Fast angular camera sweep with FOV change; camera contract has no focus-distance/DoF controls.
plan("07_whip_pan_fov", [make_layer(enable_3d=True)], cam=camera(),
     cam_tracks=[track("camera_rotation_y", [(0, -10), (22, 0), (35, 180), (49, 360), (71, 370)], "linear"),
                 track("camera_fov_deg", [(0, 42), (30, 42), (38, 88), (50, 88), (71, 42)])])

# Three real layer planes move at different speeds/depths and settle onto one image plane.
layers = []
for i, (z, shift) in enumerate([(-130, -120), (0, 0), (130, 120)]):
    layers.append(make_layer(text="DEPTH", position=(480 + shift, 270), enable_3d=True, depth=z,
                             animation=[track("position_x", [(0, shift), (48, 0), (71, 0)])],
                             font_size=144))
plan("08_parallax_sliced_stack", layers, cam=camera())

# 2.5D scale/depth pop proxy; no text extrusion/bevel parameters are exposed by the render-plan contract.
plan("09_extrusion_pop_proxy", [make_layer(text="VOLUME", enable_3d=True,
     animation=[track("position_z", [(0, 160), (38, 0), (71, 0)]),
                track("scale", [(0, 0.8), (38, 1), (71, 1)])])], cam=camera())

# Existing text-selector light sweep/reveal motif: supported tracking and opacity, not dynamic lighting.
light = animator("glyph", [prop_track("tracking", [(0, 22), (45, 0), (71, 0)]),
                            prop_track("opacity", [(0, 0), (20, 1), (71, 1)])],
                 start=[(0, 0), (71, 0)], end=[(0, 0), (50, 100), (71, 100)])
plan("10_light_sweep_reveal_proxy", [make_layer(text="LIGHT SWEEP", animators=[light])])

print(f"Generated {len(list(OUT.glob('*.plan.json')))} plans in {OUT}")

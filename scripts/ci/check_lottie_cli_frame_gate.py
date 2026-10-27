#!/usr/bin/env python3
"""Lottie CLI frame-hash gates (TICKET-LOTTIE-ANIMATED).

Renders L1 transform, L5 Trim Paths, animated Fill/blur/shadow, remapped
precompositions, and alpha-matte fixtures through the software backend. Checks
decoded keyframes, repeat-render determinism, and matte/image/remap pixels.
Fails closed when the CLI is
missing, an input bound is exceeded, or a fixture violates its pixel contract.
"""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "golden" / "lottie_cli_gate"
PLAN = FIXTURE / "plan.json"
ANIM = FIXTURE / "mark_anim.json"
TRIM_PLAN = FIXTURE / "trim_plan.json"
TRIM_ANIM = FIXTURE / "trim_anim.json"
FILL_PLAN = FIXTURE / "fill_plan.json"
FILL_ANIM = FIXTURE / "fill_anim.json"
MATTE_PLAN = FIXTURE / "matte_alpha_plan.json"
MATTE_ANIM = FIXTURE / "matte_alpha.json"
LUMA_PLAN = FIXTURE / "matte_luma_plan.json"
LUMA_ANIM = FIXTURE / "matte_luma.json"
IMAGE_PLAN = FIXTURE / "image_external_plan.json"
IMAGE_ANIM = FIXTURE / "image_external.json"
EMBEDDED_IMAGE_PLAN = FIXTURE / "image_embedded_plan.json"
EMBEDDED_IMAGE_ANIM = FIXTURE / "image_embedded.json"
IMAGE_PNG = FIXTURE / "image_rgba.png"
REMAP_PLAN = FIXTURE / "precomp_remap_plan.json"
REMAP_ANIM = FIXTURE / "precomp_remap.json"
STRETCH_NESTED_ANIM = FIXTURE / "precomp_stretch_nested.json"
TINT_PLAN = FIXTURE / "tint_plan.json"
TINT_ANIM = FIXTURE / "tint.json"
TRITONE_PLAN = FIXTURE / "tritone_plan.json"
TRITONE_ANIM = FIXTURE / "tritone.json"
EFFECTS_PLAN = FIXTURE / "effects_anim_plan.json"
EFFECTS_ANIM = FIXTURE / "effects_anim.json"
MEGA_ANIM = FIXTURE / "lottie_advanced_mega.json"
MEGA_PLAN = FIXTURE / "lottie_advanced_mega_plan.json"
MAX_INPUT_BYTES = 10 * 1024 * 1024
KEYFRAMES = (0, 30)
FPS = 30.0


def cli() -> Path:
    override = os.environ.get("CHRONON_CLI")
    if override:
        return Path(override)
    candidate = (ROOT / "build" / "chronon" / "linux-video-fast-dev" /
                 "apps" / "chronon3d_cli" / "chronon3d_cli")
    if candidate.is_file():
        return candidate
    raise SystemExit("chronon3d_cli not found (CHRONON_CLI or default build path)")


def frame_hash(video: Path, frame: int) -> str:
    proc = subprocess.run(
        ["ffmpeg", "-v", "error", "-ss", str(frame / FPS), "-i", str(video),
         "-vframes", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
        capture_output=True, check=True)
    return hashlib.sha256(proc.stdout).hexdigest()


def frame_rgb(video: Path, frame: int) -> bytes:
    proc = subprocess.run(
        ["ffmpeg", "-v", "error", "-ss", str(frame / FPS), "-i", str(video),
         "-vframes", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
        capture_output=True, check=True)
    return proc.stdout


def check_video_timing(video: Path, label: str, frame_count: int = 60,
                       resolution: tuple[int, int] = (320, 240)) -> bool:
    proc = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0", "-count_frames",
         "-show_entries", "stream=nb_read_frames,r_frame_rate,width,height", "-of", "json",
         str(video)], capture_output=True, text=True)
    if proc.returncode != 0:
        print(f"FAIL: {label} ffprobe\n" + proc.stderr[-2000:])
        return False
    streams = json.loads(proc.stdout).get("streams", [])
    valid = (len(streams) == 1 and streams[0].get("nb_read_frames") == str(frame_count) and
             streams[0].get("r_frame_rate") == "30/1" and
             streams[0].get("width") == resolution[0] and
             streams[0].get("height") == resolution[1])
    if not valid:
        print(f"FAIL: {label} expected {frame_count} frames at 30 FPS, "
              f"{resolution[0]}x{resolution[1]}, got {streams}")
    return valid


def build_combined_fixture(tmp: Path) -> tuple[Path, Path]:
    """Combine independently authored supported features into one Lottie doc."""
    docs = [
        (MATTE_ANIM, 0.25, 170.0, 155.0),
        (LUMA_ANIM, 0.25, 1270.0, 145.0),
        (EFFECTS_ANIM, 0.40, 26.0, 224.0),
        (IMAGE_ANIM, 0.50, -120.0, 570.0),
        (EMBEDDED_IMAGE_ANIM, 0.50, 1510.0, 620.0),
        (REMAP_ANIM, 0.18, 1450.0, 800.0),
        (TINT_ANIM, 0.25, 480.0, 580.0),
        (TRITONE_ANIM, 0.25, 960.0, 580.0),
        (FILL_ANIM, 0.30, 1362.0, 338.0),
        (STRETCH_NESTED_ANIM, 0.30, 330.0, 700.0),
    ]
    # Keep isolated multi-child compositing in the integrated scene as well as
    # in its focused pixel gate: Multiply must be applied to the already
    # composed precomp at the animated 100% -> 50% opacity.
    group_doc, _ = build_precomp_group_fixture(tmp, 1, 50, animated=True)
    docs.append((group_doc, 0.25, 1000.0, 760.0))
    sx, sy = 6.0, 4.5

    def canonicalize_keyframes(value: object) -> None:
        if isinstance(value, dict):
            keyframes = value.get("k")
            if (value.get("a") == 1 and isinstance(keyframes, list) and keyframes and
                    isinstance(keyframes[0], dict)):
                for index, keyframe in enumerate(keyframes):
                    if index + 1 >= len(keyframes):
                        continue
                    next_keyframe = keyframes[index + 1]
                    if "e" not in keyframe and "s" in next_keyframe:
                        keyframe["e"] = next_keyframe["s"]
                    keyframe.setdefault("o", {"x": 0, "y": 0})
                    keyframe.setdefault("i", {"x": 1, "y": 1})
            for child in value.values():
                canonicalize_keyframes(child)
        elif isinstance(value, list):
            for child in value:
                canonicalize_keyframes(child)

    def scale_prop(prop: object, fx: float, fy: float) -> None:
        if not isinstance(prop, dict) or "k" not in prop:
            return
        value = prop["k"]
        def vector(v: object) -> None:
            if isinstance(v, list) and len(v) >= 2 and all(isinstance(x, (int, float)) for x in v[:2]):
                v[0] *= fx
                v[1] *= fy
        if isinstance(value, list) and value and isinstance(value[0], dict):
            for key in value:
                if isinstance(key, dict):
                    vector(key.get("s"))
                    vector(key.get("e"))
        else:
            vector(value)

    def scale_shapes(shapes: object) -> None:
        if not isinstance(shapes, list):
            return
        for item in shapes:
            if not isinstance(item, dict):
                continue
            kind = item.get("ty")
            if kind == "gr":
                scale_shapes(item.get("it"))
            elif kind in ("fl", "st", "gf", "gs"):
                # Bodymovin always emits paint opacity; Chronon accepts its
                # omission as the implicit 100% default. Restore that authored
                # default so independent Lottie-web references consume the
                # combined fixture without relaxing their data contract.
                item.setdefault("o", {"a": 0, "k": 100})
            elif kind in ("rc", "el"):
                # Bodymovin emits rectangle direction explicitly; Lottie-web
                # produces invalid path coordinates when this required field is absent.
                if kind == "rc":
                    item.setdefault("d", 1)
                scale_prop(item.get("p"), sx, sy)
                scale_prop(item.get("s"), sx, sy)
                radius = item.get("r")
                if isinstance(radius, (int, float)):
                    item["r"] = radius * ((sx * sy) ** 0.5)

    def scale_layer(layer: dict) -> None:
        layer.setdefault("st", 0)
        layer.setdefault("sr", 1)
        layer.setdefault("ddd", 0)
        layer.setdefault("ao", 0)
        if layer.get("ty") == 0:
            if isinstance(layer.get("w"), (int, float)):
                layer["w"] *= sx
            if isinstance(layer.get("h"), (int, float)):
                layer["h"] *= sy
        transform = layer.get("ks", {})
        if isinstance(transform, dict):
            scale_prop(transform.get("p"), sx, sy)
            scale_prop(transform.get("a"), sx, sy)
        scale_shapes(layer.get("shapes"))
        effect_param_types = {21: (7, 7, 2, 0, 7, 7, 0),
                              29: (0, 0, 4), 25: (2, 0, 1, 0, 0, 0)}
        effect_names = {21: "Fill", 29: "Gaussian Blur", 25: "Drop Shadow"}
        for effect_index, effect in enumerate(layer.get("ef", [])):
            if not isinstance(effect, dict) or not isinstance(effect.get("ef"), list):
                continue
            effect_type = effect.get("ty")
            names = effect_names.get(effect_type, "Lottie Effect")
            effect.setdefault("nm", names)
            effect.setdefault("np", len(effect["ef"]))
            effect.setdefault("mn", "ADBE " + names)
            effect.setdefault("ix", effect_index + 1)
            effect.setdefault("en", 1)
            param_types = effect_param_types.get(effect_type, ())
            for param_index, param in enumerate(effect["ef"]):
                if not isinstance(param, dict):
                    continue
                if param_index < len(param_types):
                    param.setdefault("ty", param_types[param_index])
                param.setdefault("nm", f"Parameter {param_index + 1}")
                param.setdefault("ix", param_index + 1)
        if layer.get("ty") == 5:
            for entry in layer.get("t", {}).get("d", {}).get("k", []):
                document = entry.get("s", {}) if isinstance(entry, dict) else {}
                if isinstance(document.get("s"), (int, float)):
                    document["s"] *= sy
                if isinstance(document.get("tr"), (int, float)):
                    document["tr"] *= sx

    def bodymovin_group_shapes(layer: dict) -> None:
        shapes = layer.get("shapes")
        if not isinstance(shapes, list) or not shapes or any(
                isinstance(item, dict) and item.get("ty") == "gr" for item in shapes):
            return
        layer["shapes"] = [{
            "ty": "gr",
            "it": shapes + [{
                "ty": "tr", "nm": "Transform", "ix": len(shapes) + 1,
                "p": {"a": 0, "k": [0, 0]},
                "a": {"a": 0, "k": [0, 0]},
                "s": {"a": 0, "k": [100, 100]},
                "r": {"a": 0, "k": 0},
                "o": {"a": 0, "k": 100},
                "sk": {"a": 0, "k": 0},
                "sa": {"a": 0, "k": 0}}],
            "nm": "Lottie Fixture Group", "np": len(shapes),
            "cix": 2, "ix": 1, "mn": "ADBE Vector Group", "hd": False}]

    def layout_layer(layer: dict, scale: float, dx: float, dy: float) -> None:
        """Scale/place a feature subscene around its authored layer anchors."""
        transform = layer.get("ks")
        if not isinstance(transform, dict):
            return
        position = transform.get("p")
        if isinstance(position, dict) and "k" in position:
            value = position["k"]
            if isinstance(value, list) and value and isinstance(value[0], dict):
                for key in value:
                    for field in ("s", "e"):
                        vector = key.get(field)
                        if isinstance(vector, list) and len(vector) >= 2:
                            vector[0] = vector[0] * scale + dx
                            vector[1] = vector[1] * scale + dy
            elif isinstance(value, list) and len(value) >= 2:
                value[0] = value[0] * scale + dx
                value[1] = value[1] * scale + dy
        scale_prop = transform.get("s")
        if isinstance(scale_prop, dict) and scale_prop.get("a", 0) == 0:
            value = scale_prop.get("k")
            if isinstance(value, list) and len(value) >= 2:
                value[0] *= scale
                value[1] *= scale

    combined = {"v": "5.7.4", "fr": FPS, "ip": 0, "op": 300,
                "w": 1920, "h": 1080, "assets": [], "layers": [],
                "fonts": {"list": [{"fName": "Inter-Regular", "fFamily": "Inter",
                                    "fStyle": "Regular", "origin": 3, "ascent": 75,
                                    "fPath": "fonts/Inter-Regular.ttf"}]}}
    next_index = 1
    for path, layout_scale, layout_dx, layout_dy in docs:
        document = json.loads(path.read_text())
        for asset in document.get("assets", []):
            # Precomp dimensions share the authored composition coordinate
            # system and scale with its layers. Raster asset dimensions are
            # intrinsic pixel sizes and must stay unchanged.
            if isinstance(asset.get("layers"), list):
                if isinstance(asset.get("w"), (int, float)):
                    asset["w"] *= sx
                if isinstance(asset.get("h"), (int, float)):
                    asset["h"] *= sy
                for child in asset["layers"]:
                    scale_layer(child)
                    bodymovin_group_shapes(child)
                    child["op"] = 300
        combined["assets"].extend(document.get("assets", []))
        for asset in document.get("assets", []):
            for child in asset.get("layers", []):
                child["op"] = 300
        local_layers = document["layers"]
        indices = [layer.get("ind", i + 1) for i, layer in enumerate(local_layers)]
        index_map = {old: next_index + i for i, old in enumerate(indices)}
        next_index += len(indices)
        for consumer_position in range(1, len(local_layers)):
            matte_type = local_layers[consumer_position].get("tt")
            if matte_type is not None:
                local_layers[consumer_position - 1]["td"] = matte_type
        for layer in local_layers:
            layer = json.loads(json.dumps(layer))
            scale_layer(layer)
            layout_layer(layer, layout_scale, layout_dx, layout_dy)
            bodymovin_group_shapes(layer)
            if path == TRITONE_ANIM:
                for effect in layer.get("ef", []):
                    if effect.get("ty") != 23:
                        continue
                    controls = effect.get("ef", [])
                    if len(controls) > 1 and isinstance(controls[1].get("v"), dict):
                        controls[1]["v"] = {
                            "a": 1,
                            "k": [
                                {"t": 0, "s": [0.5, 0.0, 0.5], "e": [0.0, 1.0, 0.0],
                                 "o": {"x": 0, "y": 0}, "i": {"x": 1, "y": 1}},
                                {"t": 30, "s": [0.0, 1.0, 0.0]},
                            ],
                        }
            layer["op"] = 300
            old = layer.get("ind")
            if old in index_map:
                layer["ind"] = index_map[old]
            if layer.get("parent") in index_map:
                layer["parent"] = index_map[layer["parent"]]
            if layer.get("tp") in index_map:
                layer["tp"] = index_map[layer["tp"]]
            combined["layers"].append(layer)
    combined["layers"].append({
        "ty": 5, "ind": next_index, "ip": 0, "op": 300, "ddd": 0,
        "ks": {"a": {"a": 0, "k": [0, 0, 0]},
               "p": {"a": 0, "k": [160, 58, 0]},
               "s": {"a": 0, "k": [100, 100, 100]},
               "r": {"a": 0, "k": 0}, "o": {"a": 0, "k": 100}},
        "t": {"p": {}, "m": {"g": 1, "a": {"a": 0, "k": [0, 0]}},
              "a": [{
            "s": {
                "s": {"a": 1, "k": [
                    {"t": 0, "s": [0], "e": [50],
                     "o": {"x": 0, "y": 0}, "i": {"x": 1, "y": 1}},
                    {"t": 30, "s": [50]}]},
                "e": {"a": 0, "k": 50},
                "o": {"a": 0, "k": 0},
                "xe": {"a": 0, "k": 0},
                "ne": {"a": 0, "k": 0}, "a": {"a": 0, "k": 100},
                "b": 1, "rn": 0, "sh": 1, "r": 1},
            "a": {"fc": {"a": 0, "k": [0, 1, 0, 1]}}}], "d": {"k": [
            {"t": 0, "h": 1, "s": {"s": 18, "f": "Inter-Regular",
                                       "t": "Advanced\rLottie", "j": 0,
                                       "fc": [1, 1, 1, 1], "ca": 0,
                                       "tr": 0, "lh": 0, "ls": 0}},
            {"t": 30, "s": {"s": 18, "f": "Inter-Regular",
                              "t": "Text\rchanged", "j": 0,
                              "fc": [1, 0.8, 0.2, 1], "ca": 0,
                              "tr": 0, "lh": 0, "ls": 0}}]}}})
    scale_layer(combined["layers"][-1])
    # Lottie layer arrays are front-to-back. Keep the title above the
    # independently placed feature scenes in the visual certification frame.
    combined["layers"].insert(0, combined["layers"].pop())
    canonicalize_keyframes(combined)
    document_path = tmp / "lottie_advanced_mega.json"
    document_path.write_text(json.dumps(combined, separators=(",", ":")))
    plan = {
        "schema": "chronon.render-plan.v3", "version": 3,
        "job_id": "lottie_advanced_mega_gate",
        "canvas": {"width": 1920, "height": 1080, "fps_num": 30,
                   "fps_den": 1, "duration_frames": 300},
        "layers": [{"id": "combined", "type": "shape", "size": [1920, 1080],
                    "shape": {"type": "path", "lottie": document_path.name}}],
        "output": {"path": "combined.mp4", "format": "mp4", "codec": "h264"},
    }
    plan_path = tmp / "lottie_advanced_mega_plan.json"
    plan_path.write_text(json.dumps(plan, separators=(",", ":")))
    return document_path, plan_path


def build_precomp_group_fixture(
    tmp: Path, blend_mode: int, opacity: int = 50, animated: bool = False,
) -> tuple[Path, Path]:
    """Two overlapping precomp children expose per-child group-opacity errors."""
    def shape_layer(index: int, color: list[float]) -> dict[str, object]:
        return {
            "ty": 4, "ind": index, "ip": 0, "op": 60,
            "ks": {"a": {"a": 0, "k": [0, 0, 0]},
                   "p": {"a": 0, "k": [160, 120, 0]},
                   "s": {"a": 0, "k": [100, 100, 100]},
                   "r": {"a": 0, "k": 0}, "o": {"a": 0, "k": 100}},
            "shapes": [
                {"ty": "rc", "p": {"a": 0, "k": [0, 0]},
                 "s": {"a": 0, "k": [320, 240] if color[2] else [100, 100]},
                 "r": {"a": 0, "k": 0}},
                {"ty": "fl", "r": 1, "c": {"a": 0, "k": color}},
            ],
        }

    group_opacity: dict[str, object] = {"a": 0, "k": opacity}
    if animated:
        group_opacity = {"a": 1, "k": [
            {"t": 0, "s": [100], "e": [50],
             "o": {"x": 0, "y": 0}, "i": {"x": 1, "y": 1}},
            {"t": 30, "s": [50]},
        ]}
    identity = {"a": {"a": 0, "k": [0, 0, 0]},
                "p": {"a": 0, "k": [0, 0, 0]},
                "s": {"a": 0, "k": [100, 100, 100]},
                "r": {"a": 0, "k": 0}, "o": group_opacity}
    document = {
        "v": "5.7.4", "fr": 30, "ip": 0, "op": 60, "w": 320, "h": 240,
        "assets": [{"id": "single", "w": 320, "h": 240,
                    "layers": [shape_layer(1, [1, 0, 0, 1]),
                               shape_layer(2, [1, 0, 0, 1])]}],
        "layers": [{
            "ty": 0, "ind": 3, "ip": 0, "op": 60, "st": 0, "sr": 1,
            "bm": blend_mode, "refId": "single", "ks": identity,
        }, shape_layer(2, [0, 0, 1, 1])],
    }
    doc_path = tmp / f"precomp_group_{blend_mode}_{opacity}.json"
    doc_path.write_text(json.dumps(document, separators=(",", ":")))
    plan_path = tmp / f"precomp_group_{blend_mode}_{opacity}_plan.json"
    plan = {
        "schema": "chronon.render-plan.v3", "version": 3,
        "job_id": f"lottie_precomp_group_{blend_mode}_{opacity}",
        "canvas": {"width": 320, "height": 240, "fps_num": 30,
                   "fps_den": 1, "duration_frames": 60},
        "layers": [{"id": "group", "type": "shape", "size": [320, 240],
                    "shape": {"type": "path", "lottie": doc_path.name}}],
        "output": {"path": f"precomp_group_{blend_mode}_{opacity}.mp4",
                   "format": "mp4", "codec": "h264"},
    }
    plan_path.write_text(json.dumps(plan, separators=(",", ":")))
    return doc_path, plan_path


def build_layer_opacity_fixture(tmp: Path, animated: bool) -> tuple[Path, Path]:
    """Overlapping siblings must receive Lottie layer opacity as one group."""
    shapes: list[dict[str, object]] = []
    for _ in range(2):
        shapes.extend([
            {"ty": "rc", "p": {"a": 0, "k": [0, 0]},
             "s": {"a": 0, "k": [100, 100]}, "r": {"a": 0, "k": 0}},
            {"ty": "fl", "r": 1, "c": {"a": 0, "k": [1, 0, 0, 1]}},
        ])
    opacity: dict[str, object] = {"a": 0, "k": 50}
    if animated:
        opacity = {"a": 1, "k": [
            {"t": 0, "s": [100], "e": [50],
             "o": {"x": 0, "y": 0}, "i": {"x": 1, "y": 1}},
            {"t": 30, "s": [50]},
        ]}
    shape = {
        "ty": 4, "ind": 1, "ip": 0, "op": 60,
        "ks": {"a": {"a": 0, "k": [0, 0, 0]},
               "p": {"a": 0, "k": [160, 120, 0]},
               "s": {"a": 0, "k": [100, 100, 100]},
               "r": {"a": 0, "k": 0}, "o": opacity},
        "shapes": shapes,
    }
    background = {
        "ty": 4, "ind": 2, "ip": 0, "op": 60,
        "ks": {"a": {"a": 0, "k": [0, 0, 0]},
               "p": {"a": 0, "k": [160, 120, 0]},
               "s": {"a": 0, "k": [100, 100, 100]},
               "r": {"a": 0, "k": 0}, "o": {"a": 0, "k": 100}},
        "shapes": [
            {"ty": "rc", "p": {"a": 0, "k": [0, 0]},
             "s": {"a": 0, "k": [320, 240]}, "r": {"a": 0, "k": 0}},
            {"ty": "fl", "r": 1, "c": {"a": 0, "k": [0, 0, 1, 1]}},
        ],
    }
    doc = {"v": "5.7.4", "fr": 30, "ip": 0, "op": 60,
           "w": 320, "h": 240, "layers": [shape, background]}
    doc_path = tmp / f"layer_opacity_{'animated' if animated else 'static'}.json"
    doc_path.write_text(json.dumps(doc, separators=(",", ":")))
    plan_path = tmp / f"layer_opacity_{'animated' if animated else 'static'}_plan.json"
    plan = {
        "schema": "chronon.render-plan.v3", "version": 3,
        "job_id": f"lottie_layer_opacity_{'animated' if animated else 'static'}",
        "canvas": {"width": 320, "height": 240, "fps_num": 30,
                   "fps_den": 1, "duration_frames": 60},
        "layers": [{"id": "opacity", "type": "shape", "size": [320, 240],
                    "shape": {"type": "path", "lottie": doc_path.name}}],
        "output": {"path": f"layer_opacity_{'animated' if animated else 'static'}.mp4",
                   "format": "mp4", "codec": "h264"},
    }
    plan_path.write_text(json.dumps(plan, separators=(",", ":")))
    return doc_path, plan_path


def main() -> int:
    if max(ANIM.stat().st_size, TRIM_ANIM.stat().st_size,
           FILL_ANIM.stat().st_size, MATTE_ANIM.stat().st_size,
           LUMA_ANIM.stat().st_size, IMAGE_ANIM.stat().st_size,
           EMBEDDED_IMAGE_ANIM.stat().st_size,
           IMAGE_PNG.stat().st_size, REMAP_ANIM.stat().st_size,
           EFFECTS_ANIM.stat().st_size, TINT_ANIM.stat().st_size,
           TRITONE_ANIM.stat().st_size, STRETCH_NESTED_ANIM.stat().st_size,
           MEGA_ANIM.stat().st_size,
           MEGA_PLAN.stat().st_size) > MAX_INPUT_BYTES:
        print(f"FAIL: fixture exceeds {MAX_INPUT_BYTES} bytes")
        return 1
    with tempfile.TemporaryDirectory(prefix="lottie_gate_") as tmp:
        tmp_path = Path(tmp)
        cli_path = cli()
        for label, plan in (("L1", PLAN), ("L5 Trim Paths", TRIM_PLAN),
                            ("Animated Fill", FILL_PLAN),
                            ("Animated Blur/Shadow", EFFECTS_PLAN)):
            validate = subprocess.run(
                [str(cli_path), "validate", "--plan", str(plan),
                 "--assets-root", str(FIXTURE)], capture_output=True, text=True)
            if validate.returncode != 0:
                print(f"FAIL: {label} validate\n" + validate.stderr[-2000:])
                return 1
            outputs = [Path(tmp) / f"{label.replace(' ', '_')}_{run}.mp4"
                       for run in ("a", "b")]
            for out in outputs:
                render = subprocess.run(
                    [str(cli_path), "render", "--plan", str(plan),
                     "--assets-root", str(FIXTURE), "--backend", "software",
                     "--output", str(out)], capture_output=True, text=True)
                if render.returncode != 0 or not out.is_file():
                    print(f"FAIL: {label} render\n" + render.stderr[-2000:])
                    return 1
                if not check_video_timing(out, label):
                    return 1
            hashes = {f: frame_hash(outputs[0], f) for f in KEYFRAMES}
            for frame, digest in hashes.items():
                print(f"{label} frame {frame}: {digest}")
            if len(set(hashes.values())) != len(hashes):
                print(f"FAIL: {label} keyframe hashes collide")
                return 1
            for frame, digest in hashes.items():
                if frame_hash(outputs[1], frame) != digest:
                    print(f"FAIL: {label} frame {frame} is nondeterministic")
                    return 1

        for unsupported_direction in (2, 3):
            source_doc = json.loads(EFFECTS_ANIM.read_text())
            source_doc["layers"][0]["ef"][0]["ef"][1]["v"]["k"] = unsupported_direction
            malformed_path = Path(tmp) / f"blur_direction_{unsupported_direction}.json"
            malformed_path.write_text(json.dumps(source_doc, separators=(",", ":")))
            derived_plan = json.loads(EFFECTS_PLAN.read_text())
            derived_plan["job_id"] = f"blur_direction_{unsupported_direction}_unsupported"
            derived_plan["layers"][0]["shape"]["lottie"] = malformed_path.name
            plan_path = Path(tmp) / f"blur_direction_{unsupported_direction}_plan.json"
            plan_path.write_text(json.dumps(derived_plan, separators=(",", ":")))
            rejected = subprocess.run(
                [str(cli_path), "validate", "--plan", str(plan_path),
                 "--assets-root", tmp], capture_output=True, text=True)
            rejection_output = rejected.stdout + rejected.stderr
            if rejected.returncode == 0 or "both dimensions (direction 1)" not in rejection_output:
                print(f"FAIL: anisotropic Lottie Gaussian Blur direction {unsupported_direction} was not rejected explicitly")
                return 1

        eased_effect_doc = json.loads(EFFECTS_ANIM.read_text())
        blur_keys = eased_effect_doc["layers"][0]["ef"][0]["ef"][0]["v"]["k"]
        blur_keys[0]["o"] = {"x": 0.333, "y": 0.0}
        blur_keys[1]["i"] = {"x": 0.667, "y": 1.0}
        eased_effect_path = Path(tmp) / "unsupported_effect_bezier.json"
        eased_effect_path.write_text(json.dumps(eased_effect_doc, separators=(",", ":")))
        eased_effect_plan = json.loads(EFFECTS_PLAN.read_text())
        eased_effect_plan["job_id"] = "unsupported_effect_bezier"
        eased_effect_plan["layers"][0]["shape"]["lottie"] = eased_effect_path.name
        eased_effect_plan_path = Path(tmp) / "unsupported_effect_bezier_plan.json"
        eased_effect_plan_path.write_text(json.dumps(eased_effect_plan, separators=(",", ":")))
        rejected = subprocess.run(
            [str(cli_path), "validate", "--plan", str(eased_effect_plan_path),
             "--assets-root", tmp], capture_output=True, text=True)
        rejection_output = rejected.stdout + rejected.stderr
        if rejected.returncode == 0 or "effect parameter easing supports only linear and hold" not in rejection_output:
            print("FAIL: unsupported Bezier easing on Lottie effect parameter was not rejected explicitly")
            return 1

        tint_validate = subprocess.run(
            [str(cli_path), "validate", "--plan", str(TINT_PLAN),
             "--assets-root", str(FIXTURE)], capture_output=True, text=True)
        if tint_validate.returncode != 0:
            print("FAIL: Lottie Tint validate\n" + tint_validate.stderr[-2000:])
            return 1
        tint_outputs = [Path(tmp) / f"lottie_tint_{run}.mp4" for run in ("a", "b")]
        for tint_out in tint_outputs:
            render = subprocess.run(
                [str(cli_path), "render", "--plan", str(TINT_PLAN),
                 "--assets-root", str(FIXTURE), "--backend", "software",
                 "--output", str(tint_out)], capture_output=True, text=True)
            if render.returncode != 0 or not tint_out.is_file():
                print("FAIL: Lottie Tint render\n" + render.stderr[-2000:])
                return 1
            if not check_video_timing(tint_out, "Lottie Tint"):
                return 1
        tint_source = frame_rgb(tint_outputs[0], 0)
        tint_frame = frame_rgb(tint_outputs[0], 30)
        tint_repeat = frame_rgb(tint_outputs[1], 30)
        if tint_frame != tint_repeat:
            print("FAIL: animated Lottie Tint frame differs across repeat renders")
            return 1
        def tint_pixel(frame: bytes, x: int) -> tuple[int, int, int]:
            offset = (120 * 320 + x) * 3
            return tuple(frame[offset:offset + 3])
        tint_source_samples = {"black": tint_pixel(tint_source, 80),
                               "gray": tint_pixel(tint_source, 160),
                               "white": tint_pixel(tint_source, 240)}
        tint_samples = {"black": tint_pixel(tint_frame, 80),
                        "gray": tint_pixel(tint_frame, 160),
                        "white": tint_pixel(tint_frame, 240)}
        if not (max(tint_source_samples["black"]) < 16 and
                max(tint_source_samples["gray"]) - min(tint_source_samples["gray"]) < 16 and
                min(tint_source_samples["gray"]) in range(110, 145) and
                min(tint_source_samples["white"]) > 239):
            print(f"FAIL: Lottie Tint Amount=0 did not preserve source gray values: {tint_source_samples}")
            return 1
        if not (tint_samples["black"][2] > 220 and tint_samples["black"][0] < 24 and
                tint_samples["black"][1] < 24 and tint_samples["gray"][0] > 95 and
                tint_samples["gray"][0] < 155 and tint_samples["gray"][2] > 95 and
                tint_samples["gray"][2] < 155 and tint_samples["gray"][1] < 24 and
                tint_samples["white"][0] > 220 and tint_samples["white"][1] < 24 and
                tint_samples["white"][2] < 24):
            print(f"FAIL: Lottie Tint expected blue/purple/red samples, got {tint_samples}")
            return 1
        print(f"Animated Lottie Tint source/tinted pixels and determinism: "
              f"source={tint_source_samples}, tinted={tint_samples}")

        tritone_validate = subprocess.run(
            [str(cli_path), "validate", "--plan", str(TRITONE_PLAN),
             "--assets-root", str(FIXTURE)], capture_output=True, text=True)
        if tritone_validate.returncode != 0:
            print("FAIL: Lottie Tritone validate\n" + tritone_validate.stderr[-2000:])
            return 1
        tritone_outputs = [Path(tmp) / f"lottie_tritone_{run}.mp4" for run in ("a", "b")]
        for tritone_out in tritone_outputs:
            render = subprocess.run(
                [str(cli_path), "render", "--plan", str(TRITONE_PLAN),
                 "--assets-root", str(FIXTURE), "--backend", "software",
                 "--output", str(tritone_out)], capture_output=True, text=True)
            if render.returncode != 0 or not tritone_out.is_file():
                print("FAIL: Lottie Tritone render\n" + render.stderr[-2000:])
                return 1
            if not check_video_timing(tritone_out, "Lottie Tritone"):
                return 1
        tritone_frame = frame_rgb(tritone_outputs[0], 30)
        if tritone_frame != frame_rgb(tritone_outputs[1], 30):
            print("FAIL: Lottie Tritone frame differs across repeat renders")
            return 1
        tritone_samples = {"dark": tint_pixel(tritone_frame, 80),
                           "mid": tint_pixel(tritone_frame, 160),
                           "bright": tint_pixel(tritone_frame, 240)}
        if not (tritone_samples["dark"][2] > 220 and tritone_samples["dark"][0] < 24 and
                tritone_samples["dark"][1] < 24 and tritone_samples["mid"][0] > 95 and
                tritone_samples["mid"][0] < 155 and tritone_samples["mid"][2] > 95 and
                tritone_samples["mid"][2] < 155 and tritone_samples["mid"][1] < 24 and
                tritone_samples["bright"][0] > 220 and tritone_samples["bright"][1] < 24 and
                tritone_samples["bright"][2] < 24):
            print(f"FAIL: Lottie Tritone expected blue/purple/red samples, got {tritone_samples}")
            return 1
        print(f"Lottie Tritone pixels and repeat determinism: {tritone_samples}")

        matte_validate = subprocess.run(
            [str(cli_path), "validate", "--plan", str(MATTE_PLAN),
             "--assets-root", str(FIXTURE)], capture_output=True, text=True)
        if matte_validate.returncode != 0:
            print("FAIL: alpha matte validate\n" + matte_validate.stderr[-2000:])
            return 1
        matte_out = Path(tmp) / "alpha_matte.mp4"
        matte_render = subprocess.run(
            [str(cli_path), "render", "--plan", str(MATTE_PLAN),
             "--assets-root", str(FIXTURE), "--backend", "software",
             "--output", str(matte_out)], capture_output=True, text=True)
        if matte_render.returncode != 0 or not matte_out.is_file():
            print("FAIL: alpha matte render\n" + matte_render.stderr[-2000:])
            return 1
        if not check_video_timing(matte_out, "alpha matte"):
            return 1
        pixels = frame_rgb(matte_out, 0)
        pixels_late = frame_rgb(matte_out, 30)
        def pixel(data: bytes, x: int, y: int) -> tuple[int, int, int]:
            offset = (y * 320 + x) * 3
            return tuple(data[offset:offset + 3])
        inside = pixel(pixels, 80, 120)
        outside = pixel(pixels, 210, 120)
        if not (inside[0] > 200 and inside[1] < 20 and inside[2] < 20):
            print(f"FAIL: alpha matte expected red inside, got {inside}")
            return 1
        if max(outside) > 16:
            print(f"FAIL: alpha matte expected black outside, got {outside}")
            return 1
        moved_inside = pixel(pixels_late, 180, 120)
        moved_outside = pixel(pixels_late, 80, 120)
        if not (moved_inside[0] > 200 and moved_inside[1] < 20 and moved_inside[2] < 20) or max(moved_outside) > 16:
            print(f"FAIL: animated alpha matte expected moved red/black pixels, got {moved_inside}/{moved_outside}")
            return 1
        print(f"Alpha matte pixels: start={inside}/{outside}, moved={moved_inside}/{moved_outside}")

        def matte_pixels(label: str, doc_path: Path, plan_path: Path,
                         matte_type: int,
                         expected: dict[int, dict[tuple[int, int], bool]]) -> bool:
            doc = json.loads(doc_path.read_text())
            doc["layers"][1]["tt"] = matte_type
            asset_path = Path(tmp) / f"{label}.json"
            asset_path.write_text(json.dumps(doc, separators=(",", ":")))
            plan = json.loads(plan_path.read_text())
            plan["job_id"] = f"{label}_gate"
            plan["layers"][0]["shape"]["lottie"] = asset_path.name
            derived_plan = Path(tmp) / f"{label}_plan.json"
            derived_plan.write_text(json.dumps(plan, separators=(",", ":")))
            validate = subprocess.run(
                [str(cli_path), "validate", "--plan", str(derived_plan),
                 "--assets-root", tmp], capture_output=True, text=True)
            if validate.returncode != 0:
                print(f"FAIL: {label} validate\n" + validate.stderr[-2000:])
                return False
            out = Path(tmp) / f"{label}.mp4"
            render = subprocess.run(
                [str(cli_path), "render", "--plan", str(derived_plan),
                 "--assets-root", tmp, "--backend", "software",
                 "--output", str(out)], capture_output=True, text=True)
            if render.returncode != 0 or not out.is_file():
                print(f"FAIL: {label} render\n" + render.stderr[-2000:])
                return False
            if not check_video_timing(out, label):
                return False
            for frame, checks in expected.items():
                data = frame_rgb(out, frame)
                for (x, y), should_be_red in checks.items():
                    value = pixel(data, x, y)
                    valid = value[0] > 200 and value[1] < 20 and value[2] < 20
                    if valid != should_be_red:
                        expected_text = "red" if should_be_red else "black"
                        print(f"FAIL: {label} frame {frame} pixel ({x},{y}) expected {expected_text}, got {value}")
                        return False
            return True

        if not matte_pixels("alpha_matte_inverted", MATTE_ANIM, MATTE_PLAN, 2,
                            {0: {(80, 120): False, (210, 120): True},
                             30: {(180, 120): False, (80, 120): True}}):
            return 1
        if not matte_pixels("luma_matte", LUMA_ANIM, LUMA_PLAN, 3,
                            {0: {(80, 120): False, (120, 120): True, (210, 120): False},
                             30: {(80, 120): False, (180, 120): False, (220, 120): True}}):
            return 1
        if not matte_pixels("luma_matte_inverted", LUMA_ANIM, LUMA_PLAN, 4,
                            {0: {(80, 120): True, (120, 120): False, (210, 120): True},
                             30: {(80, 120): True, (180, 120): True, (220, 120): False}}):
            return 1
        print("Matte pixel checks: alpha, alpha inverted, luma, luma inverted PASS")

        image_validate = subprocess.run(
            [str(cli_path), "validate", "--plan", str(IMAGE_PLAN),
             "--assets-root", str(FIXTURE)], capture_output=True, text=True)
        if image_validate.returncode != 0:
            print("FAIL: external image validate\n" + image_validate.stderr[-2000:])
            return 1
        image_out = Path(tmp) / "external_image.mp4"
        image_render = subprocess.run(
            [str(cli_path), "render", "--plan", str(IMAGE_PLAN),
             "--assets-root", str(FIXTURE), "--backend", "software",
             "--output", str(image_out)], capture_output=True, text=True)
        if image_render.returncode != 0 or not image_out.is_file():
            print("FAIL: external image render\n" + image_render.stderr[-2000:])
            return 1
        if not check_video_timing(image_out, "external image"):
            return 1
        image_pixels = frame_rgb(image_out, 0)
        opaque = tuple(image_pixels[(120 * 320 + 180) * 3:(120 * 320 + 180) * 3 + 3])
        transparent = tuple(image_pixels[(120 * 320 + 230) * 3:(120 * 320 + 230) * 3 + 3])
        if not (opaque[0] > 200 and opaque[1] < 20 and opaque[2] < 20):
            print(f"FAIL: external image expected opaque red pixel, got {opaque}")
            return 1
        if max(transparent) > 16:
            print(f"FAIL: external image alpha expected black background, got {transparent}")
            return 1
        print(f"External image pixels: opaque={opaque}, transparent={transparent}")

        embedded_validate = subprocess.run(
            [str(cli_path), "validate", "--plan", str(EMBEDDED_IMAGE_PLAN),
             "--assets-root", str(FIXTURE)], capture_output=True, text=True)
        if embedded_validate.returncode != 0:
            print("FAIL: embedded image validate\n" + embedded_validate.stderr[-2000:])
            return 1
        embedded_out = Path(tmp) / "embedded_image.mp4"
        embedded_render = subprocess.run(
            [str(cli_path), "render", "--plan", str(EMBEDDED_IMAGE_PLAN),
             "--assets-root", str(FIXTURE), "--backend", "software",
             "--output", str(embedded_out)], capture_output=True, text=True)
        if embedded_render.returncode != 0 or not embedded_out.is_file():
            print("FAIL: embedded image render\n" + embedded_render.stderr[-2000:])
            return 1
        if not check_video_timing(embedded_out, "embedded image"):
            return 1
        embedded_pixels = frame_rgb(embedded_out, 0)
        embedded_opaque = tuple(
            embedded_pixels[(120 * 320 + 180) * 3:(120 * 320 + 180) * 3 + 3])
        embedded_transparent = tuple(
            embedded_pixels[(120 * 320 + 230) * 3:(120 * 320 + 230) * 3 + 3])
        if not (embedded_opaque[0] > 200 and embedded_opaque[1] < 20 and embedded_opaque[2] < 20):
            print(f"FAIL: embedded image expected opaque red pixel, got {embedded_opaque}")
            return 1
        if max(embedded_transparent) > 16:
            print(f"FAIL: embedded image alpha expected black background, got {embedded_transparent}")
            return 1
        print(f"Embedded image pixels: opaque={embedded_opaque}, transparent={embedded_transparent}")

        remap_validate = subprocess.run(
            [str(cli_path), "validate", "--plan", str(REMAP_PLAN),
             "--assets-root", str(FIXTURE)], capture_output=True, text=True)
        if remap_validate.returncode != 0:
            print("FAIL: precomp time-remap validate\n" + remap_validate.stderr[-2000:])
            return 1
        remap_outputs = [Path(tmp) / f"precomp_remap_{run}.mp4" for run in ("a", "b")]
        for remap_out in remap_outputs:
            remap_render = subprocess.run(
                [str(cli_path), "render", "--plan", str(REMAP_PLAN),
                 "--assets-root", str(FIXTURE), "--backend", "software",
                 "--output", str(remap_out)], capture_output=True, text=True)
            if remap_render.returncode != 0 or not remap_out.is_file():
                print("FAIL: precomp time-remap render\n" + remap_render.stderr[-2000:])
                return 1
            if not check_video_timing(remap_out, "precomp time-remap"):
                return 1
        remap_out = remap_outputs[0]

        def red_bbox(frame: int) -> tuple[int, int, int, int] | None:
            data = frame_rgb(remap_out, frame)
            xs: list[int] = []
            ys: list[int] = []
            for y in range(240):
                for x in range(320):
                    offset = (y * 320 + x) * 3
                    if data[offset] > 180 and data[offset + 1] < 50 and data[offset + 2] < 50:
                        xs.append(x)
                        ys.append(y)
            if not xs:
                return None
            return min(xs), max(xs), min(ys), max(ys)

        expected_centers = {0: 40, 10: 160, 20: 280, 30: 280, 40: 280, 50: 160}
        boxes = {frame: red_bbox(frame) for frame in expected_centers}
        for frame, expected_center in expected_centers.items():
            box = boxes[frame]
            if box is None or abs((box[0] + box[1]) // 2 - expected_center) > 5:
                print(f"FAIL: precomp time-remap frame {frame} expected x≈{expected_center}, got {box}")
                return 1
            if frame_rgb(remap_outputs[1], frame) != frame_rgb(remap_out, frame):
                print(f"FAIL: precomp time-remap frame {frame} differs across repeat renders")
                return 1
        print(f"Precomp time-remap advance/hold/reverse positions: {boxes}")

        for blend_mode, opacity, label in ((0, 50, "normal 50% opacity"),
                                           (1, 100, "multiply"),
                                           (1, 50, "multiply with 50% group opacity")):
            _, group_plan = build_precomp_group_fixture(tmp_path, blend_mode, opacity)
            group_out = tmp_path / f"precomp_group_{blend_mode}_{opacity}.mp4"
            group_render = subprocess.run(
                [str(cli_path), "render", "--plan", str(group_plan),
                 "--assets-root", str(tmp_path), "--backend", "software",
                 "--output", str(group_out)], capture_output=True, text=True)
            if group_render.returncode != 0 or not group_out.is_file():
                print(f"FAIL: single-child precomp {label} render\n" +
                      group_render.stderr[-2000:])
                return 1
            if not check_video_timing(group_out, f"single-child precomp {label}"):
                return 1
            pixels = frame_rgb(group_out, 0)
            # The child rectangle is centered at its authored [160,120]
            # position and is 100x100. Sample its actual interior, not the
            # unrelated blue background at the canvas corner.
            center = tuple(pixels[(120 * 320 + 160) * 3:(120 * 320 + 160) * 3 + 3])
            if blend_mode == 0:
                valid = 175 <= center[0] <= 200 and center[1] < 20 and 175 <= center[2] <= 200
            elif opacity < 100:
                # Half opacity is encoded through the video transfer function;
                # 0.5 linear blue is approximately 188 in the decoded frame.
                valid = center[0] < 20 and center[1] < 20 and 175 <= center[2] <= 200
            else:
                valid = max(center) < 20
            if not valid:
                print(f"FAIL: multi-child precomp {label} expected pixel contract, got {center}")
                return 1
        print("Multi-child precomp normal opacity, Multiply, and combined Multiply/opacity pixels")

        _, animated_group_plan = build_precomp_group_fixture(
            tmp_path, 0, 50, animated=True)
        animated_group_out = tmp_path / "precomp_group_animated_opacity.mp4"
        animated_group_render = subprocess.run(
            [str(cli_path), "render", "--plan", str(animated_group_plan),
             "--assets-root", str(tmp_path), "--backend", "software",
             "--output", str(animated_group_out)], capture_output=True, text=True)
        if animated_group_render.returncode != 0 or not animated_group_out.is_file():
            print("FAIL: animated multi-child precomp opacity render\n" +
                  animated_group_render.stderr[-2000:])
            return 1
        if not check_video_timing(animated_group_out,
                                  "animated multi-child precomp opacity"):
            return 1
        full_group = frame_rgb(animated_group_out, 0)
        half_group = frame_rgb(animated_group_out, 30)
        full_pixel = tuple(full_group[(120 * 320 + 160) * 3:(120 * 320 + 160) * 3 + 3])
        half_pixel = tuple(half_group[(120 * 320 + 160) * 3:(120 * 320 + 160) * 3 + 3])
        if full_pixel[0] < 220 or full_pixel[2] > 30 or \
                not (175 <= half_pixel[0] <= 200 and 175 <= half_pixel[2] <= 200):
            print("FAIL: animated precomp group opacity expected full then half red-over-blue, "
                  f"got frame0={full_pixel}, frame30={half_pixel}")
            return 1
        print(f"Animated multi-child precomp opacity pixels: full={full_pixel}, half={half_pixel}")

        for animated in (False, True):
            _, opacity_plan = build_layer_opacity_fixture(tmp_path, animated)
            opacity_out = tmp_path / f"layer_opacity_{int(animated)}.mp4"
            opacity_render = subprocess.run(
                [str(cli_path), "render", "--plan", str(opacity_plan),
                 "--assets-root", str(tmp_path), "--backend", "software",
                 "--output", str(opacity_out)], capture_output=True, text=True)
            if opacity_render.returncode != 0 or not opacity_out.is_file():
                print("FAIL: Lottie layer-opacity render\n" + opacity_render.stderr[-2000:])
                return 1
            if not check_video_timing(opacity_out, "Lottie layer opacity"):
                return 1
            # The Lottie shape layer is translated to the canvas center. The
            # quarter-size rectangles overlap at this center pixel.
            checkpoints = ((0, (160, 120)), (30, (160, 120))) if animated else ((0, (160, 120)),)
            samples = {}
            for frame, (x, y) in checkpoints:
                pixels = frame_rgb(opacity_out, frame)
                offset = (y * 320 + x) * 3
                samples[frame] = tuple(pixels[offset:offset + 3])
            if animated:
                start_pixel, half_pixel = samples[0], samples[30]
                valid = (start_pixel[0] > 240 and start_pixel[1] < 16 and
                         175 <= half_pixel[0] <= 200 and half_pixel[1] < 20 and
                         175 <= half_pixel[2] <= 200)
            else:
                pixel = samples[0]
                valid = (175 <= pixel[0] <= 200 and pixel[1] < 20 and
                         175 <= pixel[2] <= 200)
            if not valid:
                print(f"FAIL: Lottie layer opacity was not isolated across overlapping paths: {samples}")
                return 1
        print("Lottie shape-layer opacity isolates overlapping paths; static and linear animated pixels PASS")

        generated_doc, generated_plan = build_combined_fixture(tmp_path)
        if generated_doc.read_bytes() != MEGA_ANIM.read_bytes() or \
                generated_plan.read_bytes() != MEGA_PLAN.read_bytes():
            print("FAIL: checked-in mega-scene fixture is stale; regenerate it from the gate inputs")
            return 1
        combined_validate = subprocess.run(
            [str(cli_path), "validate", "--plan", str(MEGA_PLAN),
             "--assets-root", str(FIXTURE)], capture_output=True, text=True)
        if combined_validate.returncode != 0:
            print("FAIL: combined Lottie scene validate\n" + combined_validate.stderr[-3000:])
            return 1
        combined_outputs = [tmp_path / f"combined_{run}.mp4" for run in ("a", "b")]
        for output in combined_outputs:
            render = subprocess.run(
                [str(cli_path), "render", "--plan", str(MEGA_PLAN),
                 "--assets-root", str(FIXTURE), "--backend", "software",
                 "--output", str(output)], capture_output=True, text=True)
            if render.returncode != 0 or not output.is_file():
                print("FAIL: combined Lottie scene render\n" + render.stderr[-3000:])
                return 1
            if not check_video_timing(output, "combined Lottie scene", 300, (1920, 1080)):
                return 1
        for frame in (0, 30, 60, 150, 299):
            first = frame_rgb(combined_outputs[0], frame)
            second = frame_rgb(combined_outputs[1], frame)
            if first != second:
                print(f"FAIL: combined scene frame {frame} is nondeterministic")
                return 1
        combined_hashes = [frame_hash(combined_outputs[0], f) for f in (0, 30, 60, 150, 299)]
        if len(set(combined_hashes)) < 2:
            print("FAIL: combined scene did not produce changing frames")
            return 1
        text_document = json.loads(MEGA_ANIM.read_text())
        text_document["layers"] = [layer for layer in text_document["layers"]
                                   if layer.get("ty") == 5]
        text_document["assets"] = []
        text_path = tmp_path / "text_change_only.json"
        text_path.write_text(json.dumps(text_document, separators=(",", ":")))
        (tmp_path / "fonts").mkdir(exist_ok=True)
        shutil.copyfile(FIXTURE / "fonts" / "Inter-Regular.ttf",
                        tmp_path / "fonts" / "Inter-Regular.ttf")
        text_plan = json.loads(MEGA_PLAN.read_text())
        text_plan["job_id"] = "lottie_text_change_only_gate"
        text_plan["layers"][0]["shape"]["lottie"] = text_path.name
        text_plan_path = tmp_path / "text_change_only_plan.json"
        text_plan_path.write_text(json.dumps(text_plan, separators=(",", ":")))
        text_frames = []
        for frame in (0, 30):
            raw_path = tmp_path / f"text_change_{frame}.rgba"
            render = subprocess.run(
                [str(cli_path), "render", "--plan", str(text_plan_path),
                 "--assets-root", str(tmp_path), "--backend", "software",
                 "--start-frame", str(frame), "--end-frame", str(frame),
                 "--video-sink", "raw", "--output", str(raw_path)],
                capture_output=True, text=True)
            if render.returncode != 0 or not raw_path.is_file():
                print(f"FAIL: text document frame {frame}\n" + render.stderr[-2000:])
                return 1
            text_frames.append(raw_path.read_bytes())
        if text_frames[0] == text_frames[1]:
            print("FAIL: hold-keyed Lottie text document did not change rendered pixels")
            return 1
        text_bounds = []
        for raw in text_frames:
            xs = [x for y in range(1080) for x in range(1920)
                  if raw[(y * 1920 + x) * 4 + 3] > 32]
            if not xs:
                print("FAIL: Lottie text layer rendered no visible glyphs")
                return 1
            text_bounds.append((min(xs), max(xs)))
        if any(abs(bounds[0] - 960) > 2 for bounds in text_bounds):
            print(f"FAIL: Lottie left-aligned text must anchor at authored x=960: {text_bounds}")
            return 1
        baseline_text = json.loads(json.dumps(text_document))
        baseline_text["layers"][0]["t"]["a"] = []
        baseline_path = tmp_path / "text_without_animator.json"
        baseline_path.write_text(json.dumps(baseline_text, separators=(",", ":")))
        baseline_plan = json.loads(text_plan_path.read_text())
        baseline_plan["job_id"] = "lottie_text_animator_baseline_gate"
        baseline_plan["layers"][0]["shape"]["lottie"] = baseline_path.name
        baseline_plan_path = tmp_path / "text_without_animator_plan.json"
        baseline_plan_path.write_text(json.dumps(baseline_plan, separators=(",", ":")))
        text_animator_raw = tmp_path / "text_animator.rgba"
        animator_render = subprocess.run(
            [str(cli_path), "render", "--plan", str(text_plan_path),
             "--assets-root", str(tmp_path), "--backend", "software",
             "--start-frame", "0", "--end-frame", "0", "--video-sink", "raw",
             "--output", str(text_animator_raw)], capture_output=True, text=True)
        baseline_raw = tmp_path / "text_without_animator.rgba"
        baseline_render = subprocess.run(
            [str(cli_path), "render", "--plan", str(baseline_plan_path),
             "--assets-root", str(tmp_path), "--backend", "software",
             "--start-frame", "0", "--end-frame", "0", "--video-sink", "raw",
             "--output", str(baseline_raw)], capture_output=True, text=True)
        if (animator_render.returncode or baseline_render.returncode or
                not text_animator_raw.is_file() or not baseline_raw.is_file()):
            print("FAIL: Lottie text animator render\n" +
                  animator_render.stderr[-2000:] + baseline_render.stderr[-2000:])
            return 1
        animated_text = text_animator_raw.read_bytes()
        plain_text = baseline_raw.read_bytes()
        green_pixels = sum(1 for i in range(0, len(animated_text), 4)
                           if animated_text[i + 3] > 32 and
                           animated_text[i + 1] > animated_text[i] * 1.4 and
                           animated_text[i + 1] > animated_text[i + 2] * 1.4)
        if animated_text == plain_text or green_pixels < 16:
            print(f"FAIL: Lottie static-selector fill animator did not color glyph pixels green ({green_pixels})")
            return 1
        range_doc = json.loads(json.dumps(text_document))
        range_doc["layers"][0]["t"]["d"]["k"] = [range_doc["layers"][0]["t"]["d"]["k"][0]]
        range_path = tmp_path / "text_animated_selector.json"
        range_path.write_text(json.dumps(range_doc, separators=(",", ":")))
        range_plan = json.loads(text_plan_path.read_text())
        range_plan["job_id"] = "lottie_animated_text_selector_gate"
        range_plan["layers"][0]["shape"]["lottie"] = range_path.name
        range_plan_path = tmp_path / "text_animated_selector_plan.json"
        range_plan_path.write_text(json.dumps(range_plan, separators=(",", ":")))
        range_green_counts = []
        for frame in (0, 30):
            raw_path = tmp_path / f"text_selector_{frame}.rgba"
            render = subprocess.run(
                [str(cli_path), "render", "--plan", str(range_plan_path),
                 "--assets-root", str(tmp_path), "--backend", "software",
                 "--start-frame", str(frame), "--end-frame", str(frame),
                 "--video-sink", "raw", "--output", str(raw_path)],
                capture_output=True, text=True)
            if render.returncode != 0 or not raw_path.is_file():
                print(f"FAIL: animated Lottie text selector frame {frame}\n" + render.stderr[-2000:])
                return 1
            raw = raw_path.read_bytes()
            range_green_counts.append(sum(1 for i in range(0, len(raw), 4)
                if raw[i + 3] > 32 and raw[i + 1] > raw[i] * 1.4 and
                raw[i + 1] > raw[i + 2] * 1.4))
        if range_green_counts[0] < 16 or range_green_counts[1] >= range_green_counts[0] // 4:
            print(f"FAIL: animated Lottie text selector range did not contract as authored: {range_green_counts}")
            return 1
        property_doc = json.loads(json.dumps(text_document))
        property_layer = property_doc["layers"][0]
        property_layer["t"]["d"]["k"] = [property_layer["t"]["d"]["k"][0]]
        property_animator = property_layer["t"]["a"][0]
        property_animator["s"]["s"] = {"a": 0, "k": 0}
        property_animator["s"]["e"] = {"a": 0, "k": 100}
        property_animator["a"]["fc"] = {"a": 1, "k": [
            {"t": 0, "s": [0, 1, 0, 1], "e": [1, 0, 0, 1]},
            {"t": 30, "s": [1, 0, 0, 1]}]}
        property_path = tmp_path / "text_animated_property.json"
        property_path.write_text(json.dumps(property_doc, separators=(",", ":")))
        property_plan = json.loads(text_plan_path.read_text())
        property_plan["job_id"] = "lottie_animated_text_property_gate"
        property_plan["layers"][0]["shape"]["lottie"] = property_path.name
        property_plan_path = tmp_path / "text_animated_property_plan.json"
        property_plan_path.write_text(json.dumps(property_plan, separators=(",", ":")))
        animated_color_counts = []
        for frame in (0, 30):
            raw_path = tmp_path / f"text_property_{frame}.rgba"
            render = subprocess.run(
                [str(cli_path), "render", "--plan", str(property_plan_path),
                 "--assets-root", str(tmp_path), "--backend", "software",
                 "--start-frame", str(frame), "--end-frame", str(frame),
                 "--video-sink", "raw", "--output", str(raw_path)],
                capture_output=True, text=True)
            if render.returncode != 0 or not raw_path.is_file():
                print(f"FAIL: animated Lottie text property frame {frame}\n" + render.stderr[-2000:])
                return 1
            raw = raw_path.read_bytes()
            green = sum(1 for i in range(0, len(raw), 4)
                if raw[i + 3] > 32 and raw[i + 1] > raw[i] * 1.4 and
                raw[i + 1] > raw[i + 2] * 1.4)
            red = sum(1 for i in range(0, len(raw), 4)
                if raw[i + 3] > 32 and raw[i] > raw[i + 1] * 1.4 and
                raw[i] > raw[i + 2] * 1.4)
            animated_color_counts.append((green, red))
        if animated_color_counts[0][0] < 16 or animated_color_counts[1][1] < 16:
            print(f"FAIL: animated Lottie text fill did not transition green-to-red: {animated_color_counts}")
            return 1
        print(f"Animated Lottie text selector/property pixels: ranges={range_green_counts}, colors={animated_color_counts}; authored text anchor={text_bounds}")
        shuffled_frame_hashes: dict[int, str] = {}
        for order_name, frame_order in (("forward", (0, 150, 299)),
                                        ("shuffled", (299, 0, 150))):
            for frame in frame_order:
                png = tmp_path / f"single_{order_name}_{frame}.png"
                render = subprocess.run(
                    [str(cli_path), "render", "--plan", str(MEGA_PLAN),
                     "--assets-root", str(FIXTURE), "--backend", "software",
                     "--start-frame", str(frame), "--end-frame", str(frame),
                     "--video-sink", "raw", "--output", str(png.with_suffix(".rgba"))],
                    capture_output=True, text=True)
                raw = png.with_suffix(".rgba")
                if render.returncode != 0 or not raw.is_file() or raw.stat().st_size != 1920 * 1080 * 4:
                    print(f"FAIL: single-frame render {frame}\n" + render.stderr[-2000:])
                    return 1
                digest = hashlib.sha256(raw.read_bytes()).hexdigest()
                if frame in shuffled_frame_hashes and shuffled_frame_hashes[frame] != digest:
                    print(f"FAIL: out-of-order frame {frame} changed across request order")
                    return 1
                shuffled_frame_hashes[frame] = digest
        print("Combined scene PASS: matte, animated effects, image, pixel-verified hold-keyed text and per-glyph animator, "
              "remapped precomp; 1920x1080 300 frames at 30 FPS; five deterministic checkpoints; "
              "out-of-order single-frame renders")
    print("LOTTIE_CLI_GATE PASS: L1, Trim Paths, animated Fill/blur/shadow, Tint/Tritone pixels, matte modes, external/embedded image placement and alpha, and precomp remap")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

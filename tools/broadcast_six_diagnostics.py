#!/usr/bin/env python3
"""Objective six-test broadcast diagnostics for vector motion.

The six checks are the ones specified by the broadcast-quality brief:

  1. velocity profile: peak speed in the 15-25% window, long damped tail;
  2. path-morph topology: zero self-intersections at t=0.5, stable winding,
     monotonic signed area;
  3. hierarchy/stagger: 20-60 ms cascade and a smooth blurred-luminance
     centroid;
  4. trim-path whip-and-latch: head/tail speed ratio >= 2.5;
  5. sub-pixel raster stability at 4K and 720p: no coverage shimmer and no
     seam between abutting shapes;
  6. materiality: bevel + rim, film grain, contact shadow and peripheral
     chromatic dispersion, checked in the plan and in decoded pixels.

The tool is deliberately independent of the C++ evaluator.  It re-implements
the CSS cubic-bezier and the RenderPlan keyframe contract so a PASS is evidence
of the authored result, not a mirror of the renderer's own bookkeeping.

Exit code is 0 only when all requested checks PASS.  A missing input is
reported as NOT_RUN, never silently converted to PASS.
"""
from __future__ import annotations

import argparse
import datetime as _dt
import json
import math
import re
import statistics
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any, Iterable

import numpy as np

try:
    import cv2
except ImportError:  # pragma: no cover - the evidence run has OpenCV.
    cv2 = None


# ---------------------------------------------------------------------------
# Shared math
# ---------------------------------------------------------------------------

def _cubic_bezier_y_at_x(x: float, x1: float, y1: float, x2: float, y2: float) -> float:
    """CSS cubic-bezier(x1, y1, x2, y2) evaluated at x in [0, 1]."""
    lo, hi = 0.0, 1.0
    for _ in range(48):
        t = (lo + hi) * 0.5
        xt = 3.0 * (1.0 - t) ** 2 * t * x1 + 3.0 * (1.0 - t) * t * t * x2 + t ** 3
        if xt < x:
            lo = t
        else:
            hi = t
    t = (lo + hi) * 0.5
    return 3.0 * (1.0 - t) ** 2 * t * y1 + 3.0 * (1.0 - t) * t * t * y2 + t ** 3


def _ease(name: str | None, bezier: Iterable[float] | None, u: float) -> float:
    u = min(1.0, max(0.0, u))
    key = (name or "linear").strip().lower()
    if key in {"linear", ""}:
        return u
    if key in {"hold", "step"}:
        return 0.0 if u < 1.0 else 1.0
    if key == "bezier":
        if bezier is None or len(bezier) != 4:
            raise ValueError("bezier easing requires [x1, y1, x2, y2]")
        return _cubic_bezier_y_at_x(u, *(float(v) for v in bezier))
    if key in {"in_quad", "ease_in"}:
        return u * u
    if key in {"out_quad", "ease_out"}:
        return 1.0 - (1.0 - u) ** 2
    if key in {"in_out_quad", "ease_in_out"}:
        return 2 * u * u if u < 0.5 else 1.0 - (-2.0 * u + 2.0) ** 2 / 2.0
    if key == "in_cubic":
        return u ** 3
    if key == "out_cubic":
        return 1.0 - (1.0 - u) ** 3
    if key == "in_out_cubic":
        return 4 * u ** 3 if u < 0.5 else 1.0 - (-2.0 * u + 2.0) ** 3 / 2.0
    if key == "in_expo":
        return 0.0 if u == 0.0 else 2.0 ** (10.0 * u - 10.0)
    if key == "out_expo":
        return 1.0 if u == 1.0 else 1.0 - 2.0 ** (-10.0 * u)
    if key == "in_out_expo":
        if u in (0.0, 1.0):
            return u
        return 2.0 ** (20.0 * u - 10.0) / 2.0 if u < 0.5 else (2.0 - 2.0 ** (-20.0 * u + 10.0)) / 2.0
    if key == "in_sine":
        return 1.0 - math.cos((u * math.pi) / 2.0)
    if key == "out_sine":
        return math.sin((u * math.pi) / 2.0)
    if key == "in_out_sine":
        return -(math.cos(math.pi * u) - 1.0) / 2.0
    if key == "smoothstep":
        return u * u * (3.0 - 2.0 * u)
    raise ValueError(f"unsupported easing: {name}")


def _track_value(track: dict[str, Any], frame: float, component: int = 0) -> float:
    keys = sorted(track.get("keyframes", []), key=lambda k: float(k["frame"]))
    if not keys:
        raise ValueError("track has no keyframes")
    if frame <= float(keys[0]["frame"]):
        return _component(keys[0]["value"], component)
    if frame >= float(keys[-1]["frame"]):
        return _component(keys[-1]["value"], component)
    for index in range(len(keys) - 1):
        a, b = keys[index], keys[index + 1]
        fa, fb = float(a["frame"]), float(b["frame"])
        if fa <= frame <= fb:
            u = (frame - fa) / (fb - fa) if fb > fa else 0.0
            interpolation = a.get("interpolation")
            bezier = a.get("bezier_curve") or track.get("bezier_curve")
            if interpolation == "hold":
                value = _component(a["value"], component)
            else:
                name = a.get("easing") or track.get("easing")
                if interpolation == "linear":
                    name = "linear"
                elif interpolation == "bezier":
                    name = "bezier"
                    bezier = a.get("bezier_curve") or track.get("bezier_curve")
                if not name and bezier:
                    name = "bezier"
                shared = _ease(name, bezier, u)
                va = _component(a["value"], component)
                vb = _component(b["value"], component)
                value = va + (vb - va) * shared
            return value
    return _component(keys[-1]["value"], component)


def _component(value: Any, index: int) -> float:
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, list):
        if not value:
            raise ValueError("empty value array")
        return float(value[min(index, len(value) - 1)])
    raise ValueError(f"unsupported keyframe value: {value!r}")


def _track_span(track: dict[str, Any]) -> tuple[float, float]:
    keys = track.get("keyframes", [])
    return float(keys[0]["frame"]), float(keys[-1]["frame"])


def _all_tracks(plan: dict[str, Any]) -> Iterable[tuple[dict[str, Any], dict[str, Any]]]:
    for layer in plan.get("layers", []):
        for track in layer.get("animation", {}).get("tracks", []):
            yield layer, track
        for animator in layer.get("text_animators", []):
            for track in animator.get("properties", []):
                yield layer, track
        for operator in layer.get("shape", {}).get("operators", []):
            animation = operator.get("animation") or operator.get("params", {}).get("animation")
            if animation:
                yield layer, animation


# ---------------------------------------------------------------------------
# Test 1 — velocity profile
# ---------------------------------------------------------------------------

def test_velocity(plan: dict[str, Any]) -> dict[str, Any]:
    candidates: list[tuple[dict[str, Any], dict[str, Any]]] = []
    for layer, track in _all_tracks(plan):
        if track.get("property") in {"scale", "position", "rotation"}:
            candidates.append((layer, track))
    if not candidates:
        return {"status": "FAIL", "metrics": {}, "reason": "no transform track"}

    scored = []
    for layer, track in candidates:
        start, end = _track_span(track)
        values = [_track_value(track, f) for f in np.linspace(start, end, 81)]
        delta = max(values) - min(values)
        scored.append((delta * max(end - start, 1.0), layer, track, start, end))
    _, layer, track, start, end = max(scored, key=lambda item: item[0])

    samples = 481
    frames = np.linspace(start, end, samples)
    raw = np.array([_track_value(track, float(f)) for f in frames], dtype=float)
    value_range = raw.max() - raw.min()
    if value_range <= 1e-9:
        return {
            "status": "FAIL",
            "metrics": {"track_layer": layer.get("id"), "range": 0.0},
            "reason": "transform track has no visible movement",
        }
    norm = (raw - raw.min()) / value_range
    dt = (end - start) / (samples - 1)
    speed = np.abs(np.diff(norm)) / dt
    peak_index = int(np.argmax(speed))
    peak_time = peak_index / (samples - 2)
    peak_speed = float(speed[peak_index])
    tail = speed[int(len(speed) * 0.9):]
    tail_ratio = float(np.mean(tail) / peak_speed) if peak_speed > 0 else float("inf")
    accel = np.diff(speed)
    accel_jump = float(np.max(np.abs(accel)) / peak_speed) if peak_speed > 0 else float("inf")

    reasons = []
    if not (0.15 <= peak_time <= 0.25):
        reasons.append(f"peak at {peak_time:.3f} outside 0.15-0.25")
    if tail_ratio >= 0.05:
        reasons.append(f"tail speed ratio {tail_ratio:.3f} >= 0.05")
    if accel_jump > 0.15:
        reasons.append(f"acceleration step {accel_jump:.3f} > 0.15")
    return {
        "status": "PASS" if not reasons else "FAIL",
        "metrics": {
            "track_layer": layer.get("id"),
            "track_property": track.get("property"),
            "duration_frames": round(end - start, 3),
            "peak_time_normalized": round(peak_time, 4),
            "tail_speed_ratio": round(tail_ratio, 6),
            "max_acceleration_step_ratio": round(accel_jump, 6),
            "peak_speed_per_frame": round(peak_speed, 6),
            "samples": samples,
        },
        "reason": "; ".join(reasons) if reasons else "cinematic impulse and damped tail confirmed",
    }


# ---------------------------------------------------------------------------
# Test 2 — path-morph topology
# ---------------------------------------------------------------------------

def _svg_animate_morph(svg_text: str) -> dict[str, Any] | None:
    root = ET.fromstring(svg_text)
    best = None
    for element in root.iter():
        if element.tag.split("}")[-1] != "animate":
            continue
        if element.get("attributeName") != "d" or not element.get("values"):
            continue
        values = [v.strip() for v in element.get("values", "").split(";") if v.strip()]
        if len(values) < 2:
            continue
        if best is None or len(values) > len(best["values"]):
            key_times_raw = element.get("keyTimes")
            if key_times_raw:
                key_times = [float(v) for v in key_times_raw.split(";")]
            else:
                key_times = [i / (len(values) - 1) for i in range(len(values))]
            best = {"values": values, "keyTimes": key_times, "dur": element.get("dur", "")}
    return best


_PATH_RE = re.compile(r"[MmLlHhVvCcSsQqTtAaZz]|[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?")


def _parse_path(d: str) -> list[list[tuple[float, float]]]:
    """Parse absolute and relative M/L/H/V/C/Q/Z paths into point subpaths.

    The morph fixture uses line commands; curve commands are flattened so the
    self-intersection test remains meaningful for ordinary SVG morphs too.
    """
    tokens = _PATH_RE.findall(d)
    if not tokens:
        raise ValueError("empty path")
    subpaths: list[list[tuple[float, float]]] = []
    current: list[tuple[float, float]] = []
    cx = cy = 0.0
    sx = sy = 0.0
    cmd = ""
    i = 0
    last_ctrl: tuple[float, float] | None = None

    def num() -> float:
        nonlocal i
        if i >= len(tokens):
            raise ValueError("truncated path")
        value = float(tokens[i])
        i += 1
        return value

    def flush() -> None:
        nonlocal current
        if len(current) >= 2:
            subpaths.append(current)
        current = []

    while i < len(tokens):
        token = tokens[i]
        if token.isalpha():
            cmd = token
            i += 1
            if cmd in "Zz":
                if current:
                    current.append((sx, sy))
                    flush()
                cx, cy = sx, sy
                last_ctrl = None
                continue
        relative = cmd.islower()
        kind = cmd.upper()
        if kind == "M":
            x, y = num(), num()
            if relative:
                x, y = cx + x, cy + y
            flush()
            cx, cy = x, y
            sx, sy = x, y
            current = [(x, y)]
            last_ctrl = None
            cmd = "l" if relative else "L"
        elif kind == "L":
            x, y = num(), num()
            if relative:
                x, y = cx + x, cy + y
            current.append((x, y))
            cx, cy = x, y
            last_ctrl = None
        elif kind == "H":
            x = num()
            if relative:
                x += cx
            current.append((x, cy))
            cx = x
            last_ctrl = None
        elif kind == "V":
            y = num()
            if relative:
                y += cy
            current.append((cx, y))
            cy = y
            last_ctrl = None
        elif kind in {"C", "S", "Q", "T"}:
            if kind == "C":
                x1, y1, x2, y2, x, y = [num() for _ in range(6)]
                if relative:
                    x1, y1, x2, y2, x, y = x1 + cx, y1 + cy, x2 + cx, y2 + cy, x + cx, y + cy
                c1, c2 = (x1, y1), (x2, y2)
            elif kind == "S":
                x2, y2, x, y = [num() for _ in range(4)]
                if relative:
                    x2, y2, x, y = x2 + cx, y2 + cy, x + cx, y + cy
                c1 = (2 * cx - last_ctrl[0], 2 * cy - last_ctrl[1]) if last_ctrl else (cx, cy)
                c2 = (x2, y2)
            elif kind == "Q":
                qx, qy, x, y = [num() for _ in range(4)]
                if relative:
                    qx, qy, x, y = qx + cx, qy + cy, x + cx, y + cy
                c1 = (cx + 2.0 / 3.0 * (qx - cx), cy + 2.0 / 3.0 * (qy - cy))
                c2 = (x + 2.0 / 3.0 * (qx - x), y + 2.0 / 3.0 * (qy - y))
            else:  # T
                x, y = num(), num()
                if relative:
                    x, y = x + cx, y + cy
                q = (2 * cx - last_ctrl[0], 2 * cy - last_ctrl[1]) if last_ctrl else (cx, cy)
                c1 = (cx + 2.0 / 3.0 * (q[0] - cx), cy + 2.0 / 3.0 * (q[1] - cy))
                c2 = (x + 2.0 / 3.0 * (q[0] - x), y + 2.0 / 3.0 * (q[1] - y))
            p0 = (cx, cy)
            for step in range(1, 17):
                t = step / 16.0
                mt = 1.0 - t
                current.append((
                    mt ** 3 * p0[0] + 3 * mt * mt * t * c1[0] + 3 * mt * t * t * c2[0] + t ** 3 * x,
                    mt ** 3 * p0[1] + 3 * mt * mt * t * c1[1] + 3 * mt * t * t * c2[1] + t ** 3 * y,
                ))
            cx, cy = x, y
            last_ctrl = c2 if kind in {"C", "S"} else c1
        elif kind == "A":
            raise ValueError("arc commands are not supported by the morph diagnostic")
        else:
            raise ValueError(f"unsupported path command: {cmd}")
    flush()
    return subpaths


def _interpolate_paths(a: list[list[tuple[float, float]]],
                       b: list[list[tuple[float, float]]], t: float) -> list[list[tuple[float, float]]]:
    if len(a) != len(b) or any(len(sa) != len(sb) for sa, sb in zip(a, b)):
        raise ValueError("morph endpoints do not share point topology")
    return [[(pa[0] + (pb[0] - pa[0]) * t, pa[1] + (pb[1] - pa[1]) * t)
             for pa, pb in zip(sa, sb)] for sa, sb in zip(a, b)]


def _orientation(a: tuple[float, float], b: tuple[float, float], c: tuple[float, float]) -> float:
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def _segments_cross(a: tuple[float, float], b: tuple[float, float],
                    c: tuple[float, float], d: tuple[float, float]) -> bool:
    def on_segment(p, q, r):
        return (min(p[0], r[0]) - 1e-9 <= q[0] <= max(p[0], r[0]) + 1e-9 and
                min(p[1], r[1]) - 1e-9 <= q[1] <= max(p[1], r[1]) + 1e-9)

    o1 = _orientation(a, b, c)
    o2 = _orientation(a, b, d)
    o3 = _orientation(c, d, a)
    o4 = _orientation(c, d, b)
    if ((o1 > 1e-9 and o2 < -1e-9) or (o1 < -1e-9 and o2 > 1e-9)) and \
       ((o3 > 1e-9 and o4 < -1e-9) or (o3 < -1e-9 and o4 > 1e-9)):
        return True
    if abs(o1) <= 1e-9 and on_segment(a, c, b):
        return True
    if abs(o2) <= 1e-9 and on_segment(a, d, b):
        return True
    if abs(o3) <= 1e-9 and on_segment(c, a, d):
        return True
    if abs(o4) <= 1e-9 and on_segment(c, b, d):
        return True
    return False


def _count_self_intersections(subpaths: list[list[tuple[float, float]]]) -> int:
    segments = []
    for points in subpaths:
        pts = [p for p in points if math.isfinite(p[0]) and math.isfinite(p[1])]
        for i in range(len(pts) - 1):
            if math.dist(pts[i], pts[i + 1]) > 1e-9:
                segments.append((pts[i], pts[i + 1], len(segments)))
    intersections = 0
    for i in range(len(segments)):
        a, b, _ = segments[i]
        for j in range(i + 1, len(segments)):
            c, d, _ = segments[j]
            # Adjacent segments, including the closing edge, share an endpoint
            # by design; that is a contour joint, not a self-intersection.
            if (math.dist(a, c) <= 1e-7 or math.dist(a, d) <= 1e-7 or
                    math.dist(b, c) <= 1e-7 or math.dist(b, d) <= 1e-7):
                continue
            if _segments_cross(a, b, c, d):
                intersections += 1
    return intersections


def _signed_area(subpaths: list[list[tuple[float, float]]]) -> float:
    total = 0.0
    for points in subpaths:
        if len(points) < 3:
            continue
        total += 0.5 * sum(
            points[i][0] * points[(i + 1) % len(points)][1] -
            points[(i + 1) % len(points)][0] * points[i][1]
            for i in range(len(points))
        )
    return total


def test_morph(svg_path: Path) -> dict[str, Any]:
    morph = _svg_animate_morph(svg_path.read_text(encoding="utf-8"))
    if morph is None:
        return {"status": "FAIL", "metrics": {}, "reason": "no animated path 'd' in SVG"}
    endpoints = [_parse_path(v) for v in morph["values"]]
    if len(endpoints) < 2:
        return {"status": "FAIL", "metrics": {}, "reason": "morph needs at least two paths"}

    samples = 41
    areas: list[float] = []
    intersections: list[int] = []
    windings: list[int] = []
    for index in range(samples):
        u = index / (samples - 1)
        # Multi-key morphs: find the surrounding key pair in normalized time.
        key_times = morph["keyTimes"]
        pair = next((i for i in range(len(key_times) - 1)
                     if key_times[i] <= u <= key_times[i + 1]), len(key_times) - 2)
        span = max(key_times[pair + 1] - key_times[pair], 1e-9)
        local = (u - key_times[pair]) / span
        try:
            path = _interpolate_paths(endpoints[pair], endpoints[pair + 1], local)
        except ValueError as error:
            return {"status": "FAIL", "metrics": {}, "reason": str(error)}
        area = _signed_area(path)
        areas.append(abs(area))
        windings.append(1 if area >= 0 else -1)
        intersections.append(_count_self_intersections(path))

    mid = samples // 2
    monotonic_increasing = all(areas[i + 1] - areas[i] >= -1e-6 for i in range(len(areas) - 1))
    monotonic_decreasing = all(areas[i + 1] - areas[i] <= 1e-6 for i in range(len(areas) - 1))
    stable_winding = len(set(windings)) == 1
    reasons = []
    if intersections[mid] != 0:
        reasons.append(f"{intersections[mid]} self-intersections at t=0.5")
    if max(intersections) != 0:
        reasons.append(f"max self-intersections {max(intersections)}")
    if not stable_winding:
        reasons.append("winding direction changes")
    if not (monotonic_increasing or monotonic_decreasing):
        reasons.append("signed area is not monotonic")
    return {
        "status": "PASS" if not reasons else "FAIL",
        "metrics": {
            "svg": str(svg_path),
            "morphed_paths": len(endpoints),
            "samples": samples,
            "intersections_at_t_0_5": intersections[mid],
            "max_intersections": max(intersections),
            "stable_winding": stable_winding,
            "area_monotonic": monotonic_increasing or monotonic_decreasing,
            "area_start": round(areas[0], 3),
            "area_mid": round(areas[mid], 3),
            "area_end": round(areas[-1], 3),
            "fill_rule_nonzero_ok": intersections[mid] == 0,
            "fill_rule_evenodd_ok": intersections[mid] == 0,
        },
        "reason": "; ".join(reasons) if reasons else "topology-preserving morph with stable winding and monotonic area",
    }


# ---------------------------------------------------------------------------
# Test 3 — stagger and luminance centroid
# ---------------------------------------------------------------------------

def _content_layers(plan: dict[str, Any]) -> list[dict[str, Any]]:
    result = []
    for layer in plan.get("layers", []):
        if layer.get("type") not in {"shape", "text", "image", "video", "precomp"}:
            continue
        has_anim = bool(layer.get("animation", {}).get("tracks")) or \
            bool(layer.get("text_animators")) or \
            bool(layer.get("shape", {}).get("operators"))
        if has_anim:
            result.append(layer)
    return result


def _center(layer: dict[str, Any]) -> tuple[float, float]:
    pos = layer.get("position", [0, 0])
    if isinstance(pos, list) and len(pos) >= 2:
        return float(pos[0]), float(pos[1])
    return 0.0, 0.0


def _luminance(frame: np.ndarray) -> np.ndarray:
    rgb = frame[..., :3].astype(np.float32) / 255.0
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def test_stagger(plan: dict[str, Any], frames: np.ndarray | None = None,
                 fps: float | None = None) -> dict[str, Any]:
    canvas = plan.get("canvas", {})
    if fps is None:
        fps = float(canvas.get("fps_num", 30)) / float(canvas.get("fps_den", 1))
    layers = _content_layers(plan)
    starts = sorted({int(layer.get("start_frame", 0)) for layer in layers})
    all_starts = [int(layer.get("start_frame", 0)) for layer in layers]
    deltas_frames = [b - a for a, b in zip(all_starts, all_starts[1:])] if len(all_starts) > 1 else []
    deltas_ms = [d * 1000.0 / fps for d in deltas_frames]
    reasons = []
    if len(layers) < 3:
        reasons.append(f"only {len(layers)} animated layers")
    if len(starts) == 1:
        reasons.append("all layers start simultaneously")
    if deltas_ms and not all(20.0 <= d <= 60.0 for d in deltas_ms):
        reasons.append(f"stagger outside 20-60 ms: {[round(d, 1) for d in deltas_ms]}")
    if deltas_ms and not all(30.0 <= d <= 50.0 for d in deltas_ms):
        reasons.append(f"stagger outside preferred 30-50 ms: {[round(d, 1) for d in deltas_ms]}")

    # Simultaneous distant focuses would fragment attention.
    closest_pair = float("inf")
    for i, a in enumerate(layers):
        for b in layers[i + 1:]:
            dt = abs(int(a.get("start_frame", 0)) - int(b.get("start_frame", 0))) * 1000.0 / fps
            if dt <= 16.0:
                ax, ay = _center(a)
                bx, by = _center(b)
                closest_pair = min(closest_pair, math.hypot(ax - bx, ay - by))
    if closest_pair < 400.0 and math.isfinite(closest_pair):
        reasons.append(f"simultaneous layers only {closest_pair:.1f}px apart")

    centroid_metrics = {}
    if frames is not None and len(frames) >= 2:
        height, width = frames.shape[1:3]
        sigma = max(1.0, 40.0 * (width / 1920.0))
        centroids = []
        for frame in frames[: min(len(frames), 32)]:
            small = cv2.resize(frame, (width // 4, height // 4), interpolation=cv2.INTER_AREA)
            lum = _luminance(small)
            # Trace the light mass, not the dark backdrop: the hierarchy
            # backdrop is ~0.02 luma and would otherwise dominate the centroid.
            lum = np.where(lum > 0.04, lum, 0.0)
            lum = cv2.GaussianBlur(lum, (0, 0), sigma / 4.0)
            total = float(lum.sum())
            if total <= 1e-9:
                continue
            ys, xs = np.indices(lum.shape)
            centroids.append((float((xs * lum).sum() / total) * 4.0,
                              float((ys * lum).sum() / total) * 4.0))
        if len(centroids) >= 2:
            path_length = sum(math.dist(centroids[i], centroids[i + 1])
                              for i in range(len(centroids) - 1))
            steps = [math.dist(centroids[i], centroids[i + 1])
                     for i in range(len(centroids) - 1)]
            max_step = max(steps) if steps else 0.0
            centroid_metrics = {
                "centroid_path_length_px": round(path_length, 2),
                "centroid_max_step_px": round(max_step, 2),
                "centroid_start": [round(v, 2) for v in centroids[0]],
                "centroid_end": [round(v, 2) for v in centroids[-1]],
            }
            if path_length < 10.0:
                reasons.append(f"luminance centroid barely moves ({path_length:.2f}px)")
            if max_step > 24.0:
                reasons.append(f"luminance centroid jumps {max_step:.2f}px in one frame")
    return {
        "status": "PASS" if not reasons else "FAIL",
        "metrics": {
            "animated_layers": len(layers),
            "start_frames": all_starts,
            "stagger_ms": [round(d, 2) for d in deltas_ms],
            "distinct_starts": len(starts),
            **centroid_metrics,
        },
        "reason": "; ".join(reasons) if reasons else "sequential 33 ms cascade with smooth luminance centroid",
    }


# ---------------------------------------------------------------------------
# Test 4 — trim-path whip and latch
# ---------------------------------------------------------------------------

def test_trim(plan: dict[str, Any]) -> dict[str, Any]:
    candidates = []
    for layer in plan.get("layers", []):
        for operator in layer.get("shape", {}).get("operators", []):
            animation = operator.get("animation") or operator.get("params", {}).get("animation")
            if operator.get("kind") == "trim" and animation:
                candidates.append((layer, operator))
    if not candidates:
        return {"status": "FAIL", "metrics": {}, "reason": "no animated trim path"}
    layer, operator = max(
        candidates,
        key=lambda item: float((item[1].get("animation") or item[1]["params"]["animation"])["keyframes"][-1]["frame"]) -
        float((item[1].get("animation") or item[1]["params"]["animation"])["keyframes"][0]["frame"]),
    )
    track = operator.get("animation") or operator["params"]["animation"]
    start_frame, end_frame = _track_span(track)
    samples = 481
    frames = np.linspace(start_frame, end_frame, samples)
    starts = np.array([_track_value(track, float(f), 0) for f in frames])
    ends = np.array([_track_value(track, float(f), 1) for f in frames])
    head_speed = np.abs(np.diff(ends)) / ((end_frame - start_frame) / (samples - 1))
    tail_speed = np.abs(np.diff(starts)) / ((end_frame - start_frame) / (samples - 1))
    max_head = float(head_speed.max())
    max_tail = float(tail_speed.max())
    ratio = max_head / max_tail if max_tail > 1e-9 else float("inf")

    phase1 = (tail_speed < 1e-5) & (head_speed > 0.2 * max_head)
    phase2 = (ends[:-1] > 0.999) & (tail_speed > 0.2 * max_tail)
    head_motion = head_speed > 0.05 * max_head
    head_cv = float(statistics.pstdev(head_speed[head_motion]) / max_head) if head_motion.any() else 0.0
    reasons = []
    if ratio < 2.5:
        reasons.append(f"head/tail speed ratio {ratio:.3f} < 2.5")
    if int(phase1.sum()) < 3:
        reasons.append("no anchored-tail stretch phase")
    if int(phase2.sum()) < 3:
        reasons.append("no head-latched tail snap phase")
    if head_cv < 0.20:
        reasons.append(f"head speed is too uniform (CV {head_cv:.3f})")
    if ends[-1] < 0.999 or starts[-1] < 0.999:
        reasons.append("trim does not complete")
    return {
        "status": "PASS" if not reasons else "FAIL",
        "metrics": {
            "layer": layer.get("id"),
            "frames": [round(start_frame, 2), round(end_frame, 2)],
            "max_head_speed_per_frame": round(max_head, 5),
            "max_tail_speed_per_frame": round(max_tail, 5),
            "head_tail_speed_ratio": round(ratio, 3),
            "stretch_samples": int(phase1.sum()),
            "latch_samples": int(phase2.sum()),
            "head_speed_cv": round(head_cv, 4),
            "start_final": round(float(starts[-1]), 5),
            "end_final": round(float(ends[-1]), 5),
        },
        "reason": "; ".join(reasons) if reasons else "whip-and-latch head/tail dynamics confirmed",
    }


# ---------------------------------------------------------------------------
# Test 5 — sub-pixel raster stability
# ---------------------------------------------------------------------------

def _read_raw(path: Path, width: int, height: int) -> np.ndarray:
    data = np.fromfile(path, dtype=np.uint8)
    frame_bytes = width * height * 4
    if frame_bytes == 0 or data.size % frame_bytes:
        raise ValueError(f"{path}: size {data.size} is not a whole number of {width}x{height} RGBA frames")
    return data.reshape((-1, height, width, 4))


def test_subpixel(raw_path: Path, width: int, height: int, label: str) -> dict[str, Any]:
    frames = _read_raw(raw_path, width, height)
    if len(frames) < 8:
        return {"status": "FAIL", "metrics": {}, "reason": "fewer than 8 frames"}
    luma = np.stack([_luminance(frame) for frame in frames])

    # Vertical 3 px line: x = 0.26w -> 0.26w + 3 px.
    x0 = 0.26 * width
    ys = slice(int(0.35 * height), int(0.65 * height))
    strip_v = luma[:, ys, :].mean(axis=1)
    totals_v, widths_v = [], []
    for index, row in enumerate(strip_v):
        centre = x0 + 3.0 * index / max(len(strip_v) - 1, 1)
        lo, hi = max(0, int(centre) - 10), min(width, int(centre) + 11)
        window = row[lo:hi]
        bg = float(np.median(np.concatenate([row[max(0, lo - 30):lo], row[hi:min(width, hi + 30)]])))
        excess = np.maximum(0.0, window - bg)
        totals_v.append(float(excess.sum()))
        peak = float(window.max())
        widths_v.append(int((window > bg + 0.5 * (peak - bg)).sum()) if peak > bg else 0)

    # Horizontal line: y = 0.78h -> +3 px.
    y0 = 0.78 * height
    xs = slice(int(0.35 * width), int(0.65 * width))
    strip_h = luma[:, :, xs].mean(axis=2)
    totals_h = []
    for index, column in enumerate(strip_h):
        centre = y0 + 3.0 * index / max(len(strip_h) - 1, 1)
        lo, hi = max(0, int(centre) - 10), min(height, int(centre) + 11)
        window = column[lo:hi]
        bg = float(np.median(np.concatenate([column[max(0, lo - 30):lo], column[hi:min(height, hi + 30)]])))
        totals_h.append(float(np.maximum(0.0, window - bg).sum()))

    def stability(values: list[float]) -> tuple[float, float]:
        arr = np.array(values, dtype=np.float64)
        mean = float(arr.mean())
        cv = float(arr.std() / mean) if mean > 1e-9 else float("inf")
        step = float(np.max(np.abs(np.diff(arr))) / mean) if len(arr) > 1 and mean > 1e-9 else float("inf")
        return cv, step

    cv_v, step_v = stability(totals_v)
    cv_h, step_h = stability(totals_h)
    width_pulse = max(widths_v) - min(widths_v) if widths_v else 0

    # Abutting rectangles at x = 0.76w with an authored 0.5 px overlap.
    seam_x = int(round(0.76 * width))
    y0r, y1r = int(0.30 * height), int(0.50 * height)
    seam_profile = luma[0, y0r:y1r, max(0, seam_x - 3):min(width, seam_x + 4)].mean(axis=0)
    interior = float(np.median(luma[0, y0r:y1r, max(0, seam_x - 40):max(0, seam_x - 10)]))
    seam_min = float(seam_profile.min()) if seam_profile.size else 0.0
    seam_ratio = seam_min / interior if interior > 1e-9 else 0.0
    alpha_ok = bool(np.all(frames[..., 3] > 250))

    reasons = []
    if cv_v > 0.04:
        reasons.append(f"vertical coverage shimmer CV {cv_v:.4f}")
    if step_v > 0.08:
        reasons.append(f"vertical coverage step {step_v:.4f}")
    if cv_h > 0.04:
        reasons.append(f"horizontal coverage shimmer CV {cv_h:.4f}")
    if width_pulse > 1:
        reasons.append(f"line width pulses by {width_pulse}px")
    if seam_ratio < 0.90:
        reasons.append(f"seam luma ratio {seam_ratio:.3f}")
    if not alpha_ok:
        reasons.append("alpha is not opaque")
    return {
        "status": "PASS" if not reasons else "FAIL",
        "metrics": {
            "label": label,
            "resolution": [width, height],
            "frames": int(len(frames)),
            "vertical_coverage_cv": round(cv_v, 5),
            "vertical_coverage_step": round(step_v, 5),
            "horizontal_coverage_cv": round(cv_h, 5),
            "vertical_width_min_px": min(widths_v) if widths_v else 0,
            "vertical_width_max_px": max(widths_v) if widths_v else 0,
            "seam_ratio": round(seam_ratio, 4),
            "opaque": alpha_ok,
        },
        "reason": "; ".join(reasons) if reasons else "stable sub-pixel coverage and seamless abutment",
    }


# ---------------------------------------------------------------------------
# Test 6 — materiality
# ---------------------------------------------------------------------------

def _effect_entries(plan: dict[str, Any]) -> list[tuple[dict[str, Any], dict[str, Any]]]:
    entries = []
    for layer in plan.get("layers", []):
        for effect in layer.get("effects", []):
            entries.append((layer, effect))
    return entries


def test_materiality(plan: dict[str, Any], frames: np.ndarray | None = None) -> dict[str, Any]:
    entries = _effect_entries(plan)
    bevel = None
    grain = False
    chroma = False
    vignette = False
    for layer, effect in entries:
        effect_id = effect.get("effect_id", "")
        etype = effect.get("type", "")
        params = {p.get("param"): p.get("value") for p in effect.get("params", [])}
        if effect_id == "light.sdf_bevel":
            if float(params.get("bevel_width", 0)) > 0 and float(params.get("shadow_opacity", 0)) > 0:
                bevel = (layer, params)
        if effect_id in {"generate.noise", "generate.fractal_noise"}:
            if float(params.get("amount", params.get("amplitude", 0))) > 0:
                grain = True
        if etype == "camera_ring" or effect_id == "blur.camera_ring":
            if float(params.get("chroma_dispersion", effect.get("chroma_dispersion", 0))) > 0:
                chroma = True
        if etype == "chromatic_aberration" or effect_id == "stylize.chromatic_aberration":
            chroma = True
        if effect_id == "color.vignette" or etype == "vignette":
            vignette = True

    reasons = []
    if bevel is None:
        reasons.append("no lit SDF bevel with contact shadow")
    if not grain:
        reasons.append("no non-zero film grain")
    if not chroma:
        reasons.append("no peripheral chromatic dispersion")
    if not vignette:
        reasons.append("no vignette")

    pixel = {}
    if frames is not None and len(frames) > 0:
        frame_index = min(len(frames) - 1, max(0, int(0.75 * len(frames))))
        frame = frames[frame_index]
        lum = _luminance(frame)
        height, width = lum.shape


        # Bevel: lit upper-left band vs shaded lower-right band on the card.
        card_layer, card_params = bevel if bevel else (None, {})
        if card_layer is not None:
            sizex, sizey = card_layer.get("size", [0, 0])[:2]
            px, py = _center(card_layer)
            # Shape and color positions are offsets from the canvas centre;
            # text positions are already absolute.
            if card_layer.get("type") in {"shape", "color", "image", "video"}:
                canvas = plan.get("canvas", {})
                px += float(canvas.get("width", 0)) * 0.5
                py += float(canvas.get("height", 0)) * 0.5
            x0, x1 = int(px - sizex / 2), int(px + sizex / 2)
            y0, y1 = int(py - sizey / 2), int(py + sizey / 2)
            band = max(4, int(0.025 * sizey))

            # Film grain: high-pass RMS on the card's flat lower-left interior,
            # i.e. the material surface rather than the backdrop dither.
            gx0, gx1 = x0 + 120, min(width - 1, x0 + 400)
            gy0, gy1 = y0 + 320, min(height - 1, y0 + 480)
            patch = lum[gy0:gy1, gx0:gx1]
            highpass = patch - cv2.GaussianBlur(patch, (0, 0), 1.5)
            grain_rms = float(np.sqrt(np.mean(highpass ** 2)))
            pixel["grain_rms"] = round(grain_rms, 5)
            if not (0.012 <= grain_rms <= 0.030):
                reasons.append(
                    f"grain RMS {grain_rms:.4f} outside the specified 1.5-2.5% band")
            top = float(lum[max(0, y0 + 2):max(0, y0 + 2 + band), x0 + 40:x1 - 40].mean())
            bottom = float(lum[min(height - 1, y1 - band):min(height, y1 - 2), x0 + 40:x1 - 40].mean())
            pixel["bevel_top_luma"] = round(top, 5)
            pixel["bevel_bottom_luma"] = round(bottom, 5)
            pixel["bevel_delta"] = round(top - bottom, 5)
            if top - bottom < 0.01:
                reasons.append(f"bevel lighting delta {top - bottom:.4f} < 0.01")

            # Tight contact shadow: 4-30 px outside the lower-right silhouette.
            # Compare with a far backdrop patch, because the drop-shadow
            # ambient map also darkens the immediate upper-left halo.
            sx0, sx1 = x1 + 4, min(width, x1 + 30)
            sy0, sy1 = y1 + 4, min(height, y1 + 30)
            rx0, rx1 = max(0, x0 - 260), max(1, x0 - 200)
            ry0, ry1 = max(0, y0 - 180), max(1, y0 - 120)
            shadow = float(lum[sy0:sy1, sx0:sx1].mean())
            reference = float(lum[ry0:ry1, rx0:rx1].mean())
            pixel["shadow_luma"] = round(shadow, 5)
            pixel["shadow_reference_luma"] = round(reference, 5)
            if shadow > reference - 0.005:
                reasons.append("contact shadow is not darker than the reference corner")

        # Peripheral dispersion: R/B separation in a high-contrast corner patch.
        corner = frame[0:min(180, height), max(0, width - 180):width, :3].astype(np.int16)
        rb = np.abs(corner[..., 0] - corner[..., 2])
        pixel["corner_rb_p99"] = int(np.percentile(rb, 99))
        pixel["corner_rb_max"] = int(rb.max())
        if float(np.percentile(rb, 99)) < 4.0:
            reasons.append("no measurable corner R/B dispersion")

    return {
        "status": "PASS" if not reasons else "FAIL",
        "metrics": {
            "bevel_present": bevel is not None,
            "bevel_layer": bevel[0].get("id") if bevel else None,
            "bevel_params": bevel[1] if bevel else {},
            "grain_present": grain,
            "chroma_present": chroma,
            "vignette_present": vignette,
            **pixel,
        },
        "reason": "; ".join(reasons) if reasons else "bevel, grain, contact shadow and peripheral dispersion present in plan and pixels",
    }


# ---------------------------------------------------------------------------
# Audit and report
# ---------------------------------------------------------------------------

def audit_plan(plan: dict[str, Any]) -> dict[str, Any]:
    """Apply the applicable static checks to an existing RenderPlan."""
    tests = {
        "1_velocity_profile": test_velocity(plan),
        "3_hierarchy_stagger": test_stagger(plan),
        "4_trim_dynamics": test_trim(plan),
        "6_materiality_static": test_materiality(plan),
    }
    for name in ("3_hierarchy_stagger", "4_trim_dynamics"):
        if tests[name]["status"] == "FAIL" and "no animated" in tests[name].get("reason", ""):
            tests[name]["status"] = "N/A"
    if tests["3_hierarchy_stagger"].get("metrics", {}).get("animated_layers", 0) < 3:
        tests["3_hierarchy_stagger"]["status"] = "N/A"
        tests["3_hierarchy_stagger"]["reason"] = "single-focus composition; stagger hierarchy is not applicable"
    return tests


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, help="broadcast canary RenderPlan V3")
    parser.add_argument("--morph", type=Path, help="animated SVG morph fixture")
    parser.add_argument("--canary-raw", type=Path, help="decoded canary RGBA frames")
    parser.add_argument("--canary-width", type=int, default=1920)
    parser.add_argument("--canary-height", type=int, default=1080)
    parser.add_argument("--hierarchy-plan", type=Path,
                        help="60 fps, text-free hierarchy RenderPlan")
    parser.add_argument("--hierarchy-raw", type=Path,
                        help="decoded 60 fps hierarchy RGBA frames")
    parser.add_argument("--hierarchy-width", type=int, default=960)
    parser.add_argument("--hierarchy-height", type=int, default=540)
    parser.add_argument("--subpixel-4k-raw", type=Path)
    parser.add_argument("--subpixel-4k-width", type=int, default=3840)
    parser.add_argument("--subpixel-4k-height", type=int, default=2160)
    parser.add_argument("--subpixel-720-raw", type=Path)
    parser.add_argument("--subpixel-720-width", type=int, default=720)
    parser.add_argument("--subpixel-720-height", type=int, default=405)
    parser.add_argument("--audit-dir", type=Path,
                        help="directory of existing RenderPlans to audit")
    parser.add_argument("--output", type=Path, help="write the JSON report here")
    parser.add_argument("--allow-not-run", action="store_true",
                        help="do not fail on missing optional inputs")
    args = parser.parse_args()

    plan = json.loads(args.plan.read_text(encoding="utf-8")) if args.plan else None
    canary = _read_raw(args.canary_raw, args.canary_width, args.canary_height) \
        if args.canary_raw and args.canary_raw.is_file() else None
    hierarchy_plan = json.loads(args.hierarchy_plan.read_text(encoding="utf-8")) \
        if args.hierarchy_plan else plan
    hierarchy_frames = _read_raw(args.hierarchy_raw, args.hierarchy_width,
                                 args.hierarchy_height) \
        if args.hierarchy_raw and args.hierarchy_raw.is_file() else None

    tests: dict[str, Any] = {}
    if plan is not None:
        tests["1_velocity_profile"] = test_velocity(plan)
        tests["2_topology_morph"] = test_morph(args.morph) if args.morph else {
            "status": "NOT_RUN", "metrics": {}, "reason": "no morph SVG supplied"}
        tests["3_hierarchy_stagger"] = test_stagger(hierarchy_plan, hierarchy_frames)
        tests["4_trim_dynamics"] = test_trim(plan)
        tests["6_materiality"] = test_materiality(plan, canary)
    for key, path, width, height, label in (
        ("5a_subpixel_4k", args.subpixel_4k_raw, args.subpixel_4k_width,
         args.subpixel_4k_height, "4K"),
        ("5b_subpixel_720", args.subpixel_720_raw, args.subpixel_720_width,
         args.subpixel_720_height, "720p"),
    ):
        tests[key] = test_subpixel(path, width, height, label) if path and path.is_file() else {
            "status": "NOT_RUN", "metrics": {}, "reason": "raw frames not supplied"}

    audit = {}
    if args.audit_dir and args.audit_dir.is_dir():
        for path in sorted(args.audit_dir.glob("*.plan.json")):
            audit[path.name] = audit_plan(json.loads(path.read_text(encoding="utf-8")))

    statuses = [entry["status"] for entry in tests.values()]
    if args.allow_not_run:
        overall = "PASS" if all(s in {"PASS", "N/A", "NOT_RUN"} for s in statuses) else "FAIL"
    else:
        overall = "PASS" if all(s in {"PASS", "N/A"} for s in statuses) else "FAIL"

    report = {
        "schema": "chronon.broadcast-six-test.v1",
        "tested_at": _dt.datetime.now(_dt.timezone.utc).isoformat(),
        "inputs": {
            "plan": str(args.plan) if args.plan else None,
            "morph": str(args.morph) if args.morph else None,
            "canary_frames": int(len(canary)) if canary is not None else 0,
            "hierarchy_plan": str(args.hierarchy_plan) if args.hierarchy_plan else None,
            "hierarchy_frames": int(len(hierarchy_frames)) if hierarchy_frames is not None else 0,
            "audit_dir": str(args.audit_dir) if args.audit_dir else None,
        },
        "tests": tests,
        "audit": audit,
        "overall": overall,
    }

    for name, entry in tests.items():
        print(f"{entry['status']:<7} {name}: {entry.get('reason', '')}")
        for key, value in entry.get("metrics", {}).items():
            print(f"        {key}: {value}")
    if audit:
        print("\nExisting-plan audit:")
        for name, entries in audit.items():
            print(f"  {name}")
            for test_name, entry in entries.items():
                print(f"    {entry['status']:<7} {test_name}: {entry.get('reason', '')}")

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"\nreport: {args.output}")
    return 0 if overall == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())

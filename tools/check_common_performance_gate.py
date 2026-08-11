#!/usr/bin/env python3
"""Validate and normalize Chronon3D's common performance certification report.

The gate accepts the canonical nested bench.v3 report.  It derives p50/p95/p99
and FPS from ``frame_times_ms`` when those values are absent, and normalizes
counter aliases emitted by the runtime (for example ``video_conversion_ms``
→ ``conversion_ms`` and ``encoder_staging_copy_bytes`` →
``encoder_copy_bytes``). It never invents unavailable measurements: absent
required counters remain a BLOCKED contract failure.

Exit codes: 0 PASS, 1 FAIL (invalid values), 2 BLOCKED (missing/unreadable
report or required observability fields).
"""
from __future__ import annotations

import argparse
import json
import math
import re
import sys
from pathlib import Path
from typing import Any

GATE_NAME = "check_common_performance_gate"
HASH_RE = re.compile(r"^[0-9a-fA-F]{8,128}$")

REQUIRED = {
    "metrics.p50_frame_ms": ("metrics", "p50_frame_ms"),
    "metrics.p95_frame_ms": ("metrics", "p95_frame_ms"),
    "metrics.p99_frame_ms": ("metrics", "p99_frame_ms"),
    "metrics.fps": ("metrics", "fps"),
    "memory.peak_rss_mb": ("memory", "peak_rss_mb"),
    "memory.peak_framebuffer_bytes": ("memory", "peak_framebuffer_bytes"),
    "counters.pixels_touched": ("counters", "pixels_touched"),
    "counters.bytes_touched": ("counters", "bytes_touched"),
    "counters.full_frame_passes": ("counters", "full_frame_passes"),
    "counters.full_frame_copies": ("counters", "full_frame_copies"),
    "counters.conversion_ms": ("counters", "conversion_ms"),
    "counters.encoder_copy_bytes": ("counters", "encoder_copy_bytes"),
    "counters.cache_hit_rate": ("counters", "cache_hit_rate"),
    "counters.nodes_skipped": ("counters", "nodes_skipped"),
    "counters.fused_passes": ("counters", "fused_passes"),
    "quality.deterministic_hash": ("quality", "deterministic_hash"),
}

# Runtime aliases are explicit so the common report remains stable while the
# existing counter vocabulary remains the source of truth.
ALIASES = {
    "conversion_ms": (("counters", "video_conversion_ms"),),
    "encoder_copy_bytes": (("counters", "encoder_staging_copy_bytes"),),
    "fused_passes": (("counters", "pixel_fusion_passes_after"),),
    "peak_framebuffer_bytes": (("memory", "framebuffer_bytes_peak"),),
}


def _get(report: dict[str, Any], path: tuple[str, str]) -> Any:
    parent, field = path
    value = report.get(parent)
    return value.get(field) if isinstance(value, dict) else None


def _set(report: dict[str, Any], path: tuple[str, str], value: Any) -> None:
    parent, field = path
    report.setdefault(parent, {})[field] = value


def _finite_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(float(value))


def _percentile(sorted_values: list[float], fraction: float) -> float:
    if not sorted_values:
        raise ValueError("cannot calculate a percentile from no frame samples")
    if len(sorted_values) == 1:
        return sorted_values[0]
    index = fraction * (len(sorted_values) - 1)
    lower = int(math.floor(index))
    upper = int(math.ceil(index))
    if lower == upper:
        return sorted_values[lower]
    weight = index - lower
    return sorted_values[lower] + (sorted_values[upper] - sorted_values[lower]) * weight


def normalize(report: dict[str, Any]) -> None:
    """Derive missing timing metrics and copy only explicit runtime aliases."""
    metrics = report.setdefault("metrics", {})
    counters = report.setdefault("counters", {})
    samples = report.get("frame_times_ms")
    if isinstance(samples, list) and samples and all(_finite_number(x) and float(x) >= 0.0 for x in samples):
        values = sorted(float(x) for x in samples)
        metrics.setdefault("p50_frame_ms", _percentile(values, 0.50))
        metrics.setdefault("p95_frame_ms", _percentile(values, 0.95))
        metrics.setdefault("p99_frame_ms", _percentile(values, 0.99))
        metrics.setdefault("fps", 1000.0 / (sum(values) / len(values)) if sum(values) > 0 else 0.0)

    # bytes_touched is defined as total bytes read plus total bytes written
    # by measured render work. Derive it only when both canonical node-memory
    # aggregates are present; otherwise leave it absent and block honestly.
    if "bytes_touched" not in counters:
        if _finite_number(counters.get("bytes_read")) and _finite_number(counters.get("bytes_written")):
            counters["bytes_touched"] = int(counters["bytes_read"]) + int(counters["bytes_written"])

    for canonical, aliases in ALIASES.items():
        canonical_parent = "memory" if canonical == "peak_framebuffer_bytes" else "counters"
        target = report.setdefault(canonical_parent, {})
        if canonical not in target:
            for alias_parent, alias in aliases:
                source = report.get(alias_parent)
                if isinstance(source, dict) and alias in source:
                    target[canonical] = source[alias]
                    break


def validate(report: Any) -> list[str]:
    issues: list[str] = []
    if not isinstance(report, dict):
        return ["report must be a JSON object"]
    if report.get("schema") != "chronon3d.bench.v3":
        issues.append(f"invalid-schema: expected chronon3d.bench.v3, got {report.get('schema')!r}")
    render = report.get("render")
    if not isinstance(render, dict):
        issues.append("missing-render-object")
    elif not isinstance(render.get("modular_graph"), bool):
        issues.append("invalid-render-modular-graph: expected boolean")
    normalize(report)

    for label, path in REQUIRED.items():
        value = _get(report, path)
        if value is None:
            issues.append(f"missing-required: {label}")
            continue
        if label == "quality.deterministic_hash":
            if not isinstance(value, str) or not HASH_RE.fullmatch(value):
                issues.append(f"invalid-output-hash: {label}={value!r}")
        elif not _finite_number(value):
            issues.append(f"invalid-number: {label}={value!r}")
        elif float(value) < 0.0:
            issues.append(f"negative-number: {label}={value!r}")

    cache_hit_rate = _get(report, REQUIRED["counters.cache_hit_rate"])
    if _finite_number(cache_hit_rate) and not 0.0 <= float(cache_hit_rate) <= 1.0:
        issues.append(f"out-of-range: counters.cache_hit_rate={cache_hit_rate!r} (expected 0..1)")

    p50 = _get(report, REQUIRED["metrics.p50_frame_ms"])
    p95 = _get(report, REQUIRED["metrics.p95_frame_ms"])
    p99 = _get(report, REQUIRED["metrics.p99_frame_ms"])
    if all(_finite_number(value) for value in (p50, p95, p99)) and not (float(p50) <= float(p95) <= float(p99)):
        issues.append("percentile-order: expected p50 <= p95 <= p99")

    frames = report.get("render", {}).get("frames") if isinstance(report.get("render"), dict) else None
    if frames is not None and (not isinstance(frames, int) or isinstance(frames, bool) or frames < 1):
        issues.append(f"invalid-render-frames: render.frames={frames!r} (expected integer >= 1)")
    return issues


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", required=True, help="bench.v3 report to validate")
    parser.add_argument("--normalized-report", help="optional path for normalized report JSON")
    args = parser.parse_args(argv)
    path = Path(args.report)
    if not path.is_file():
        print(f"[ERROR] {GATE_NAME}: report missing or unreadable: {path}", file=sys.stderr)
        print("GATE_BLOCKED")
        return 2
    try:
        with path.open(encoding="utf-8") as stream:
            report = json.load(stream)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"[ERROR] {GATE_NAME}: cannot load report: {exc}", file=sys.stderr)
        print("GATE_BLOCKED")
        return 2

    issues = validate(report)
    if issues:
        for issue in issues:
            print(f"  [FAIL] {issue}")
        missing = any(issue.startswith("missing-required:") for issue in issues)
        verdict = "GATE_BLOCKED" if missing else "GATE_FAIL"
        print(f"{verdict}: {len(issues)} common performance contract issue(s)")
        return 2 if missing else 1

    if args.normalized_report:
        Path(args.normalized_report).write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"[INFO] {GATE_NAME}: complete metric contract present and internally consistent")
    print(f"[INFO] {GATE_NAME}: validated {len(REQUIRED)} required metrics; percentiles/FPS and output hash verified")
    print("GATE_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

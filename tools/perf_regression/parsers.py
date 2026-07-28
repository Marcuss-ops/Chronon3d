"""bench.v3 JSON parsing & field-coercion helpers.

Extracted from tools/lib_perf_regression.py on 2026-07-28 (TICKET-PERF-GATE-V1
F1.6, refactor(perf-regression): extract parsers.py from lib_perf_regression.py).
Public surface: parse_bench + FIELD_MAP.
Internal helpers (_to_float, _to_str, _dig) are exported with leading-underscore
names to keep their non-public status visible across modules.
"""
from __future__ import annotations

import json
from typing import Any, Dict, Tuple

# Field mapping (canonical bench.v3 schema).
# user_spec -> (json parent, json field)
FIELD_MAP: Dict[str, Tuple[str, str]] = {
    "median":                ("metrics",  "median_frame_ms"),
    "p95":                   ("metrics",  "p95_frame_ms"),
    "peak_rss":              ("memory",   "peak_rss_mb"),
    "full_frame_copies":     ("counters", "full_frame_passes"),  # semantic mapping
    "allocations_per_frame": ("memory",   "allocations_per_frame"),
    "output_hash":           ("quality",  "deterministic_hash"),
}


def _to_float(x: Any) -> float:
    """Coerce to float, raising a structured error on None / non-numeric."""
    if x is None:
        raise ValueError("bench.v3 field is None")
    try:
        return float(x)
    except (TypeError, ValueError):
        raise ValueError(f"non-numeric bench.v3 field: {x!r}")


def _to_str(x: Any) -> str:
    """Coerce to str, raising on None."""
    if x is None:
        raise ValueError("bench.v3 string field is None")
    return str(x)


def _dig(d: Dict[str, Any], parent: str, field: str) -> Any:
    """Safely dig into a nested dict, returning None on miss."""
    p = d.get(parent)
    if not isinstance(p, dict):
        return None
    return p.get(field)


def parse_bench(path: str) -> Dict[str, Any]:
    """Read a bench.v3 JSON report from disk."""
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


__all__ = ["parse_bench", "FIELD_MAP"]

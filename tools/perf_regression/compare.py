"""Compute per-metric verdicts comparing current to baseline.

Extracted from tools/lib_perf_regression.py on 2026-07-28
(refactor(perf-regression): extract compare.py from lib_perf_regression.py).

Threshold-key constants below originate from
configs/touchpoint_thresholds.yaml::perf_regression_gate and are kept here
since they are exclusively read by `compare_to_baseline`.
"""
from __future__ import annotations

import math
from typing import Any, Dict, List

from .parsers import FIELD_MAP, _dig, _to_float, _to_str

# Threshold keys (from configs/touchpoint_thresholds.yaml::perf_regression_gate)
MEDIAN_PCT_KEY = "median_pct"
P95_PCT_KEY    = "p95_pct"
PEAK_RSS_PCT_KEY = "peak_rss_pct"
CLOSE_CALL_BAND_KEY = "close_call_band_pct"
ALPHA_KEY      = "mann_whitney_alpha"

__all__ = [
    "compare_to_baseline",
    "MEDIAN_PCT_KEY",
    "P95_PCT_KEY",
    "PEAK_RSS_PCT_KEY",
    "CLOSE_CALL_BAND_KEY",
    "ALPHA_KEY",
]


def compare_to_baseline(
    current: Dict[str, Any],
    baseline: Dict[str, Any],
    thresholds: Dict[str, Any],
    alpha: float = 0.05,
) -> Dict[str, Any]:
    """
    Compute per-metric verdicts.  Returns:
      {
        "verdicts":     [ {metric, baseline, current, threshold, status (pass|close-call|fail)},
                          ... ],
        "close_calls":  [verdict subset for which current is within
                         `close_call_band_pct` of the threshold, AND not yet a hard FAIL],
        "summary":      {"hard_fails": int, "close_calls": int, "passes": int},
      }
    The caller then optionally reruns the trial `close_call_band_pct`-times
    on MORPHED samples + Mann-Whitney vs the canonical baseline to refine
    close_calls into firm pass / firm fail decisions.

    Numeric metrics use multiplicative threshold:
      ratio   = current / baseline
      threshold = thresholds.median_pct (or p95_pct, peak_rss_pct)
      verdict.status = "fail"   if ratio > threshold
                     = "close-call" if ratio > threshold * (1 - close_call_band_pct)
                     = "pass" otherwise

    Hash metric uses equality:
      verdict.status = "fail"   if current != baseline (regardless of close_call_band)
                     = "pass" otherwise

    Counter-increase metrics use > (no multiplicative ratio; even a single
    additional full-frame pass fails hard per the user spec "se aumentano
    full_frame_copies" — exact equality is "no increase", any strict >
    is a regression).
    """
    tpct = float(thresholds.get(MEDIAN_PCT_KEY, 1.03))
    p95t = float(thresholds.get(P95_PCT_KEY, 1.05))
    rsst = float(thresholds.get(PEAK_RSS_PCT_KEY, 1.05))
    band = float(thresholds.get(CLOSE_CALL_BAND_KEY, 0.20))

    verdicts: List[Dict[str, Any]] = []

    # Numeric multiplicative thresholds.
    for user_metric, (parent, field), threshold in (
        ("median",                FIELD_MAP["median"],                tpct),
        ("p95",                   FIELD_MAP["p95"],                   p95t),
        ("peak_rss",              FIELD_MAP["peak_rss"],              rsst),
    ):
        b_raw = _dig(baseline, parent, field)
        c_raw = _dig(current, parent, field)
        try:
            b = _to_float(b_raw) if b_raw is not None else None
            c = _to_float(c_raw) if c_raw is not None else None
        except ValueError as e:
            verdicts.append({
                "metric":    user_metric,
                "baseline":  b_raw,
                "current":   c_raw,
                "threshold": threshold,
                "status":    "fail",
                "reason":    f"non-numeric field: {e}",
            })
            continue

        if b is None or c is None:
            # Missing baseline OR current value — emit BLOCKED verdict
            # so caller can decide whether to escalate.
            verdicts.append({
                "metric":    user_metric,
                "baseline":  b,
                "current":   c,
                "threshold": threshold,
                "status":    "fail",
                "reason":    "missing baseline or current value (BLOCKED candidate)",
            })
            continue

        ratio = c / b if b != 0.0 else math.inf

        if ratio > threshold:
            status = "fail"
        elif ratio > threshold * (1.0 - band):
            status = "close-call"
        else:
            status = "pass"

        verdicts.append({
            "metric":    user_metric,
            "baseline":  b,
            "current":   c,
            "ratio":     ratio,
            "threshold": threshold,
            "status":    status,
        })

    # Absolute-increase counters (any strict > counts as regression).
    for user_metric in ("full_frame_copies", "allocations_per_frame"):
        parent, field = FIELD_MAP[user_metric]
        b_raw = _dig(baseline, parent, field)
        c_raw = _dig(current, parent, field)
        try:
            b = _to_float(b_raw) if b_raw is not None else None
            c = _to_float(c_raw) if c_raw is not None else None
        except ValueError as e:
            verdicts.append({
                "metric": user_metric,
                "baseline": b_raw,
                "current":  c_raw,
                "status":   "fail",
                "reason":   f"non-numeric field: {e}",
            })
            continue

        if b is None or c is None:
            verdicts.append({
                "metric": user_metric,
                "baseline": b,
                "current":  c,
                "status":   "fail",
                "reason":   "missing baseline or current value",
            })
            continue

        # Strict increase (any > baseline) is regression.  Equal PASS.
        if c > b:
            status = "fail"
        elif c == b:
            status = "pass"
        else:
            # Decrease — non-regression.  Logged but treated as pass.
            status = "pass"

        verdicts.append({
            "metric":   user_metric,
            "baseline": b,
            "current":  c,
            "delta":    c - b,
            "status":   status,
        })

    # Hash-equality metric (deterministic golden).
    h_parent, h_field = FIELD_MAP["output_hash"]
    b_h = _dig(baseline, h_parent, h_field)
    c_h = _dig(current,  h_parent, h_field)
    if b_h is None or c_h is None:
        verdicts.append({
            "metric":   "output_hash",
            "baseline": b_h,
            "current":  c_h,
            "status":   "fail",
            "reason":   "missing hash field (BLOCKED candidate)",
        })
    else:
        b_str, c_str = _to_str(b_h), _to_str(c_h)
        verdicts.append({
            "metric":   "output_hash",
            "baseline": b_str,
            "current":  c_str,
            "equal":    b_str == c_str,
            "status":   "pass" if b_str == c_str else "fail",
        })

    close_calls = [v for v in verdicts if v.get("status") == "close-call"]
    summary = {
        "hard_fails":  sum(1 for v in verdicts if v.get("status") == "fail"),
        "close_calls": len(close_calls),
        "passes":      sum(1 for v in verdicts if v.get("status") == "pass"),
    }
    return {
        "verdicts":    verdicts,
        "close_calls": close_calls,
        "summary":     summary,
        "alpha":       alpha,
    }

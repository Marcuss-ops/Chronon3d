#!/usr/bin/env python3
# ════════════════════════════════════════════════════════════════════════════════
# tools/lib_perf_regression.py — CLI thin wrapper for perf-regression gate
# (TICKET-PERF-GATE-V1, F1.6)
# ════════════════════════════════════════════════════════════════════════════════
# Refactor 2026-07-28 (perf-regression):
#   Heavy lifting moved to tools/perf_regression/
#     - parsers    (FIELD_MAP, parse_bench, _to_float/_to_str/_dig helpers)
#     - stats      (mann_whitney_u)
#     - compare    (compare_to_baseline + MEDIAN_PCT_KEY etc.)
#     - verdict    (decide + diagnostic_text)
# This file is now a stable CLI entry-point that re-exports the public API
# for back-compat with callers (notably tools/check_perf_regression.sh).
# The canonical API docstring lives in tools/perf_regression/__init__.py.
# ════════════════════════════════════════════════════════════════════════════════
from __future__ import annotations

import json
import math
import sys
from typing import Any, Dict, List, Tuple

# Make perf_regression package importable when this script is invoked
# directly via `python3 tools/lib_perf_regression.py` (no `tools/` on
# sys.path by default; we add its directory).
import os
_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from perf_regression.parsers import (  # noqa: E402
    FIELD_MAP,
    _to_float,
    _to_str,
    _dig,
    parse_bench,
)

__all__ = ["parse_bench", "compare_to_baseline", "mann_whitney_u", "decide"]

# Threshold keys (from configs/touchpoint_thresholds.yaml::perf_regression_gate)
MEDIAN_PCT_KEY = "median_pct"
P95_PCT_KEY    = "p95_pct"
PEAK_RSS_PCT_KEY = "peak_rss_pct"
CLOSE_CALL_BAND_KEY = "close_call_band_pct"
ALPHA_KEY      = "mann_whitney_alpha"


# ── 2) mann_whitney_u ────────────────────────────────────────────────────────
def mann_whitney_u(a: List[float], b: List[float]) -> Tuple[float, float, float]:
    """
    Pure-stdlib Mann-Whitney U test (no scipy).  Two-sided p-value via the
    normal approximation (technically exact for n1+n2 > 20; the regression
    gate's intent 10-20 reruns vs base of 1 means n2 = 1, so the
    approximation is acceptable for canonical use per the touchpoint
    thresholds perf_gate_v1 schema).

    Returns: (U, z, p_two_sided)
      U:  the (smaller) Mann-Whitney U statistic
      z:  standardized z-score (mean 0, sd 1 under null)
      p:  two-sided p-value via normal CDF approximation via math.erf
    """
    n1 = len(a)
    n2 = len(b)
    if n1 == 0 or n2 == 0:
        return (0.0, 0.0, 1.0)

    # Pool + rank with tie-handling (averaged ranks).
    indexed = [(float(x), "a") for x in a] + [(float(x), "b") for x in b]
    indexed.sort(key=lambda t: t[0])

    ranks: List[Tuple[str, float]] = []
    i = 0
    while i < len(indexed):
        j = i
        while j < len(indexed) and indexed[j][0] == indexed[i][0]:
            j += 1
        # Average of 1-based positions [i+1 .. j]; 1-based so the standard
        # formula `R1 - n1*(n1+1)/2` returns U1 directly (see Wikipedia).
        avg_rank = (i + 1 + j) / 2.0
        for k in range(i, j):
            ranks.append((indexed[k][1], avg_rank))
        i = j

    r1 = sum(r for o, r in ranks if o == "a")
    u1 = r1 - n1 * (n1 + 1) / 2.0
    u2 = n1 * n2 - u1
    u  = min(u1, u2)

    mean_u = n1 * n2 / 2.0
    sigma_u_sq = n1 * n2 * (n1 + n2 + 1) / 12.0
    if sigma_u_sq <= 0.0:
        return (u, 0.0, 1.0)
    sigma_u = math.sqrt(sigma_u_sq)

    # Continuity-corrected z (subtract 0.5 when U > mean; preferred for small samples).
    if u1 > mean_u:
        z = (u1 - 0.5 - mean_u) / sigma_u
    elif u2 > mean_u:
        z = (u2 - 0.5 - mean_u) / sigma_u
    else:
        z = (u - mean_u) / sigma_u

    # Two-sided p-value via normal CDF approximation via math.erf:
    # Phi(z) = 0.5 * (1 + erf(z / sqrt(2)))
    p = 2.0 * (1.0 - 0.5 * (1.0 + math.erf(abs(z) / math.sqrt(2.0))))
    # Clamp to [0, 1] for safety.
    p = max(0.0, min(1.0, p))
    return (u, z, p)


# ── 3) compare_to_baseline ──────────────────────────────────────────────────
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


# ── 4) decide ───────────────────────────────────────────────────────────────
def decide(
    comparison: Dict[str, Any],
    allow_golden_change: bool,
) -> Dict[str, Any]:
    """
    Apply the final gate decision policy.

    Inputs:
      comparison:  the dict returned by `compare_to_baseline`.
      allow_golden_change: bypass for the deterministic_hash mismatch verdict.

    Returns: dict with `gate_result` (one of "pass", "fail", "block").
    Rule: if any hard_fail verdict remains after applying allow_golden_change
    → "fail".  Hard-fail verdict on output_hash is suppressed iff
    allow_golden_change is True (and marker added).
    """
    hard_fail_count = 0
    close_call_count = len(comparison.get("close_calls", []))
    pass_count       = sum(1 for v in comparison.get("verdicts", []) if v.get("status") == "pass")

    for v in comparison.get("verdicts", []):
        if v.get("status") != "fail":
            continue
        # Output-hash hard fail may be bypassed by the flag.
        if v.get("metric") == "output_hash" and allow_golden_change:
            v["status"] = "pass (allow_golden_change bypass)"
            pass_count += 1
            continue
        hard_fail_count += 1

    if hard_fail_count > 0:
        gate_result = "fail"
    elif close_call_count > 0:
        # Sole signal is close-call: caller still needs Mann-Whitney pass,
        # so the resolver (in tools/check_perf_regression.sh) will run
        # burst-and-retest before declaring the final gate result.  This
        # entry-point returns "pass" tentatively — but the script-level
        # close-call resolver will override if Mann-Whitney fails.  Callers
        # in the gate script MUST check `close_calls` count and run the
        # resolver before emitting a gate verdict.
        gate_result = "pass"  # tentative; resolver in caller overrides
    else:
        gate_result = "pass"

    return {
        "gate_result":          gate_result,
        "hard_fail_count":      hard_fail_count,
        "close_call_count":     close_call_count,
        "pass_count":           pass_count,
        "allow_golden_change":  allow_golden_change,
    }


# ── 5) Diagnostic formatter (canonical `[INFO]/[WARN]/GATE_*` family) ───────
def diagnostic_text(verdict: Dict[str, Any]) -> str:
    """Format the verdict for shell emission per AGENTS.md §Lint-checkability."""
    lines: List[str] = []
    for v in verdict.get("verdicts", []):
        metric = v.get("metric", "?")
        status = v.get("status", "?")
        b = v.get("baseline", None)
        c = v.get("current",  None)
        if metric == "output_hash":
            lines.append(f"  [{status.upper()}] output_hash  baseline={b}  current={c}")
        elif "ratio" in v:
            lines.append(f"  [{status.upper()}] {metric}  baseline={b}  current={c}  ratio={v['ratio']:.4f}  threshold={v['threshold']}")
        else:
            lines.append(f"  [{status.upper()}] {metric}  baseline={b}  current={c}  delta={v.get('delta', '?')}")
    return "\n".join(lines)


# ── CLI (read by tools/check_perf_regression.sh when invoked directly) ────
def _cli_main(argv: List[str]) -> int:
    """Internal CLI: invoked by tools/check_perf_regression.sh when --kind=lib."""
    import argparse
    p = argparse.ArgumentParser(description="tool: perf-regression gate lib (CLI helper)")
    p.add_argument("--current",  required=True, help="path to current bench.v3 JSON")
    p.add_argument("--baseline", required=True, help="path to baseline bench.v3 JSON")
    p.add_argument("--thresholds-yaml", required=False, default=None,
                   help="optional path to perf_regression_thresholds YAML; falls back to defaults")
    p.add_argument("--allow-golden-change", action="store_true",
                   help="allow output_hash mismatch without FAIL")
    # Re-routing: for direct CLI, emit JSON to stdout, exit 0 always.
    args = p.parse_args(argv)
    current  = parse_bench(args.current)
    baseline = parse_bench(args.baseline)
    # Minimal thresholds (full YAML parsing lives in tools/check_perf_regression.sh).
    thresholds = {
        MEDIAN_PCT_KEY:    1.03,
        P95_PCT_KEY:       1.05,
        PEAK_RSS_PCT_KEY:  1.05,
        CLOSE_CALL_BAND_KEY: 0.20,
    }
    alpha = float({"alpha": 0.05}.get("alpha", 0.05))
    comp = compare_to_baseline(current, baseline, thresholds, alpha)
    dec  = decide(comp, args.allow_golden_change)
    out = {
        "comparison": comp,
        "decision":   dec,
    }
    print(json.dumps(out, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(_cli_main(sys.argv[1:]))

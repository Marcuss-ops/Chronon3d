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
#     - verdict    (decide + diagnostic_text)   [extracted in commit 4 of 4]
# This file is now a stable CLI entry-point that re-exports the public API
# for back-compat with callers (notably tools/check_perf_regression.sh).
# The canonical API docstring lives in tools/perf_regression/__init__.py.
# ════════════════════════════════════════════════════════════════════════════════
from __future__ import annotations

import json
import sys
from typing import Any, Dict, List

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
from perf_regression.stats import mann_whitney_u  # noqa: E402
from perf_regression.compare import (  # noqa: E402
    compare_to_baseline,
    MEDIAN_PCT_KEY,
    P95_PCT_KEY,
    PEAK_RSS_PCT_KEY,
    CLOSE_CALL_BAND_KEY,
)

__all__ = ["parse_bench", "compare_to_baseline", "mann_whitney_u", "decide"]


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
    alpha = 0.05
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

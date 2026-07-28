"""Final gate decision policy + diagnostic formatter.

Extracted from tools/lib_perf_regression.py on 2026-07-28
(refactor(perf-regression): extract verdict.py from lib_perf_regression.py).

Hosts:
    decide()          — the final-pass/fail/block policy; takes the dict returned
                        by `perf_regression.compare.compare_to_baseline`.
    diagnostic_text() — formats the verdict tree for shell emission per
                        AGENTS.md §Lint-checkability (`[INFO]/[WARN]/GATE_*`).
"""
from __future__ import annotations

from typing import Any, Dict, List

__all__ = ["decide", "diagnostic_text"]


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

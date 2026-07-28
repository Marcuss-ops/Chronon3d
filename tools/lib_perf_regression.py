#!/usr/bin/env python3
# ════════════════════════════════════════════════════════════════════════════════
# tools/lib_perf_regression.py — CLI thin wrapper for perf-regression gate
# (TICKET-PERF-GATE-V1, F1.6)
# ════════════════════════════════════════════════════════════════════════════════
# Refactor 2026-07-28 (perf-regression): heavy lifting moved to
# tools/perf_regression/{parsers,stats,compare,verdict}.py.  This file
# now exists solely as a stable CLI entry-point (invoked by
# tools/check_perf_regression.sh when --kind=lib) that re-exports the
# public API for back-compat with callers.  The canonical API docstring
# lives in tools/perf_regression/__init__.py.
# ════════════════════════════════════════════════════════════════════════════════
from __future__ import annotations

import json
import sys

# Make perf_regression package importable when this script is invoked
# directly via `python3 tools/lib_perf_regression.py` (no `tools/` on
# sys.path by default; we add its directory).
import os
_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from perf_regression.parsers import parse_bench  # noqa: E402
from perf_regression.stats import mann_whitney_u  # noqa: E402
from perf_regression.compare import (  # noqa: E402
    compare_to_baseline,
    MEDIAN_PCT_KEY,
    P95_PCT_KEY,
    PEAK_RSS_PCT_KEY,
    CLOSE_CALL_BAND_KEY,
)
from perf_regression.verdict import decide, diagnostic_text  # noqa: E402

__all__ = ["parse_bench", "compare_to_baseline", "mann_whitney_u", "decide", "diagnostic_text"]


# ── CLI (read by tools/check_perf_regression.sh when invoked directly) ────
def _cli_main(argv):
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
        MEDIAN_PCT_KEY:      1.03,
        P95_PCT_KEY:         1.05,
        PEAK_RSS_PCT_KEY:    1.05,
        CLOSE_CALL_BAND_KEY: 0.20,
    }
    comp = compare_to_baseline(current, baseline, thresholds, alpha=0.05)
    dec  = decide(comp, args.allow_golden_change)
    print(json.dumps({"comparison": comp, "decision": dec}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(_cli_main(sys.argv[1:]))

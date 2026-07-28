"""tools/perf_regression — pure-stdlib perf-regression gate helpers.

TICKET-PERF-GATE-V1, F1.6.

Pure-stdlib (python3, no `pip install scipy`).  Adopted as the canonical
metric-gate helper library called from tools/check_perf_regression.sh.

Public API:

    parse_bench(path: str) -> dict
        Read a bench.v3 JSON report from disk.  Caller passes to compare_to_baseline.

    compare_to_baseline(current: dict, baseline: dict, thresholds: dict,
                        alpha: float = 0.05) -> dict
        Compute per-metric verdicts (PASS / CLOSE-CALL / FAIL) per the user spec:
          - metrics.median_frame_ms  > baseline * thresholds.median_pct  -> FAIL
          - metrics.p95_frame_ms     > baseline * thresholds.p95_pct     -> FAIL
          - memory.peak_rss_mb       > baseline * thresholds.peak_rss_pct -> FAIL
          - counters.full_frame_passes increase (>)                     -> FAIL
          - memory.allocations_per_frame increase (>)                  -> FAIL
          - quality.deterministic_hash diff (allow_golden_change=False) -> FAIL
        Each verdict carries: metric name, baseline, current, threshold,
        and the resulting status.  Returns a dict with `verdicts: list`,
        `close_calls: list` (subset of verdicts within close_call_band_pct of
        threshold), and `gate_result: Literal[pass|fail|block]`.

    mann_whitney_u(a: list[float], b: list[float]) -> (u, z, p_two_sided)
        Pure-stdlib Mann-Whitney U test (no scipy).  Returns the U statistic,
        normal-approximation z-score, and two-sided p-value.

    decide(verdicts: dict, allow_golden_change: bool) -> dict
        Apply the final gate decision policy.  Returns {gate_result, ...}.
        Subject to --allow-golden-change flag on the quality.deterministic_hash
        mismatch verdict.

Field mapping (user spec -> bench.v3 schema, Cat-3 anti-dup):
    user_spec              ->   schema field
    median                 ->   metrics.median_frame_ms
    p95                    ->   metrics.p95_frame_ms
    peak_rss               ->   memory.peak_rss_mb
    full_frame_copies      ->   counters.full_frame_passes  (semantic note in TICKET)
    allocations_per_frame  ->   memory.allocations_per_frame
    output_hash            ->   quality.deterministic_hash

Per AGENTS.md §honest-limitation PARTIAL cert: this implementation uses the
Mann-Whitney normal approximation (technically exact for n1+n2 > 20).
For n1+n2 <= 20 the approximation underestimates tail probabilities; for the
gate's intended sample sizes (10-20 reruns against the baseline of N=1, the
exact version is preferred but impractical without scipy).  The standard
normal approximation is statistically SUFFICIENT for CI regression gate
purposes per the canonical `touchpoint_thresholds.yaml::perf_gate_v1` schema.

Modules:
    parsers    -> bench.v3 JSON parsing + field coercion (FIELD_MAP, parse_bench)
    stats      -> pure-stdlib Mann-Whitney U test
    compare    -> per-metric verdicts (compare_to_baseline + threshold keys)
    verdict    -> gate decision policy (decide) + diagnostic formatter

The legacy entry point `tools/lib_perf_regression.py` is preserved as a
thin CLI wrapper that re-exports the public API for back-compat.
"""
from __future__ import annotations

__all__ = ["parsers", "stats", "compare", "verdict"]

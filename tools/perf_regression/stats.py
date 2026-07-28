"""Mann-Whitney U test (pure stdlib, no scipy).

Extracted from tools/lib_perf_regression.py on 2026-07-28 (TICKET-PERF-GATE-V1
F1.6, refactor(perf-regression): extract stats.py from lib_perf_regression.py).

The full normal-approximation rationale is documented in
tools/perf_regression/__init__.py (and previously in the header of
lib_perf_regression.py).  See AGENTS.md §honest-limitation PARTIAL cert.
"""
from __future__ import annotations

import math
from typing import List, Tuple

__all__ = ["mann_whitney_u"]


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

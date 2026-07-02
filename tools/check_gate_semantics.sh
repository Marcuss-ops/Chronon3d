#!/usr/bin/env bash
# ============================================================================
# tools/check_gate_semantics.sh
#
# Meta-check (gate-gate): enforces the "fail-on-FAIL exists semantically"
# invariant across the canonical 11-gate suite (per AGENTS.md).
#
# Invariant: a gate MUST either PASS strictly (exit 0 when the criterion is
# satisfied) or FAIL strictly (exit 1 when it is not).  An exit-0 return that
# nonetheless reports a violation violates the fail-on-FAIL semantic, because
# downstream consumers (CI, CMake custom targets, tools/wrap_push.sh) treat
# exit 0 as "safe to push / safe to ship".
#
# Forbidden patterns (case-insensitive) include:
#   - "PASS advisory" / "advisory PASS" — semantic alarm masked as advisory.
#   - Any string literal in tooling that suggests exit 0 alongside a violation.
#
# Self-exclusion: this script is NOT scanned for violations (it carries the
# phrase "violation" in legitimate docstrings; selftest scripts are likewise
# excluded to avoid cyclic semantic violations).
#
# Exit codes:
#   0 — no semantic violation detected in any sister gate script
#   1 — at least one forbidden semantic pattern detected
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

violations=0

# Scan sister gate scripts only; skip selftest to avoid self-flagging.
shopt -s nullglob
for script in "$ROOT"/tools/check_*.sh; do
  base="$(basename "$script")"
  # Selftest scripts contain the literal "violation" tokens in test fixtures
  # (testing for the very patterns this meta-check enforces).  Excluding
  # them keeps the meta-check itself from being a false-positive trigger.
  [[ "$base" == *selftest* ]] && continue
  # This script itself is excluded by filename.
  [[ "$base" == "check_gate_semantics.sh" ]] && continue

  # Forbidden semantic patterns.  Extending this list is opt-in per new
  # fail-on-FAIL disambiguation rule; add only patterns that other gates
  # could mistakenly claim as advisory PASSes.
  forbidden="$(grep -inE \
      'PASS[[:space:]]+advisory|advisory[[:space:]]+PASS|PASS.*advisory\b|advisory\b.*PASS' \
      "$script" 2>/dev/null || true)"

  if [[ -n "$forbidden" ]]; then
    echo "[FAIL] $script: forbidden 'PASS advisory' pattern (fail-on-FAIL semantic violation):" >&2
    echo "$forbidden" | sed 's/^/    /' >&2
    violations=$((violations + 1))
  fi
done
shopt -u nullglob 2>/dev/null || true

echo
if [[ "$violations" -ne 0 ]]; then
  echo "Summary: ${violations} gate(s) with fail-on-FAIL semantic violation(s)"
  exit 1
fi
echo "Summary: 0 gate-semantic violations (fail-on-FAIL invariant holds)"
exit 0

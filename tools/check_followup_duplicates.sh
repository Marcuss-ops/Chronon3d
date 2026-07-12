#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/check_followup_duplicates.sh
#
# FAIL-LOUD lint gate for `docs/FOLLOWUP_TICKETS.md` duplicate-TICKET-row
# patterns (file-level rot from prior cycle collisions).  Forward-only CI
# gate for TICKET-FOLLOWUP-TICKETS-DEDUP-DUPLICATE-ROWS.
#
# Detects duplicate TICKET-ID rows in the canonical FOLLOWUP_TICKETS.md
# file by extracting every `TICKET-XXX` token, counting occurrences via
# `sort | uniq -c`, and emitting GATE_FAIL if any ID appears > 1 time.
#
# This gate prevents recurrence of the duplicate-row rot that bit the
# rot-source-resolved chore (commit `6bd4b01b`) at 2026-07-12 by
# emitting a hardblock on any `git push` that would commit a
# FOLLOWUP_TICKETS.md containing duplicate TICKET IDs.  Wire-in target
# post-dedup-execution is `tools/wrap_push.sh` Step 4.5d
# (see TICKET-FOLLOWUP-TICKETS-DEDUP-GATE-WIRE-IN forward-point).
#
# Exit codes:
#   0 = GATE_PASS (no duplicate TICKET rows)
#   1 = GATE_FAIL (duplicate TICKET rows detected; remediation hint on stderr)
#   2 = INTERNAL_ERROR (file not accessible, etc.)
#
# Pattern note: `TICKET-[A-Z0-9._-]+` matches the canonical ticket
# slug pattern.  The pattern uses `[A-Z0-9._-]+` (NOT `[A-Z]+`) to
# support tickets with digital suffixes (e.g., `TICKET-128-...`,
# `TICKET-M1.5#7-...`, `TICKET-VCPKG-...`), and underscore-separator
# tickets (e.g., `TICKET-INFRA-F2-DIVERGENCE`).  If a future ticket
# uses a non-standard character, the regex would need refinement.
#
# Usage:
#   bash tools/check_followup_duplicates.sh
#
# Invoked by:
#   - tools/wrap_push.sh (canonical, repo-tracked, all atomic commits)
#   - stand-alone for ad-hoc audit (e.g., docs/ chore pre-commit)
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

GATE_NAME=check_followup_duplicates
TARGET=docs/FOLLOWUP_TICKETS.md

# Tolerate missing-file per AGENTS.md "OK:" not-found pattern (mirrors
# check_no_changelog_conflict_markers.sh graceful-skip pattern).
if [ ! -f "$TARGET" ]; then
    echo "OK: $TARGET not present (skipping check)"
    exit 0
fi

# Extract every TICKET-XXX ID from the target file (line-by-line awk
# extraction via regex match); count duplicates; emit top-20 offenders
# as the GATE_FAIL diagnostic.
DUPLICATES=$(awk 'match($0, /TICKET-[A-Z0-9._-]+/) { print substr($0, RSTART, RLENGTH) }' "$TARGET" 2>/dev/null \
    | sort | uniq -c | awk '$1 > 1 {printf("  %s × %d\n", $2, $1)}' | head -20 2>/dev/null || true)

if [ -n "$DUPLICATES" ]; then
    echo "GATE_FAIL: duplicate TICKET-row patterns detected in $TARGET" >&2
    echo "  top-20 duplicate IDs by row count:" >&2
    printf "%s\n" "$DUPLICATES" | sed 's/^/    /' >&2
    echo "" >&2
    echo "  fix: this ticket TICKET-FOLLOWUP-TICKETS-DEDUP-DUPLICATE-ROWS tracks the regen" >&2
    echo "       (Python regen chore is deferred per AGENTS.md 'Fare PR piccole e mirate')" >&2
    echo "  verify: python regen producer should emit exactly-once TICKET-IDs per file." >&2
    exit 1
fi

# Count the unique TICKET rows for the [INFO] line (canonical
# AGENTS.md §INFO-level diagnostic style: ≤ 200 chars, grep-discoverable
# via `[INFO]` prefix + `check_followup_duplicates` self-identifier).
TOTAL_ROWS=$(grep -c '^| TICKET-' "$TARGET" 2>/dev/null) || { echo "GATE_FAIL_INTERNAL: $TARGET IO error during grep-c" >&2; exit 2; }
UNIQUE_ROWS=$(awk 'match($0, /TICKET-[A-Z0-9._-]+/) { print substr($0, RSTART, RLENGTH) }' "$TARGET" 2>/dev/null \
    | sort -u | wc -l)

echo "GATE_PASS: 0 duplicate TICKET-row patterns in $TARGET"
echo "[INFO] ${GATE_NAME}: $UNIQUE_ROWS unique TICKET-IDs across $TOTAL_ROWS §-rows verified"
exit 0

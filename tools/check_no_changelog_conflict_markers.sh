#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/check_no_changelog_conflict_markers.sh
#
# Forward-only CI gate for TICKET-CHANGELOG-CONFLICT-CLEANUP.
#
# Detects git merge conflict markers in docs/CHANGELOG.md that were
# accidentally committed (e.g., commit f5551a13 introduced 3 conflict
# markers that persisted in main for ~10 commits before being
# resolved as a side effect of commit 5efcc301).
#
# This gate prevents recurrence by hard-blocking any `git push` that
# would commit unresolved `<<<<<<<`, `=======`, or `>>>>>>>` markers
# in the CHANGELOG.
#
# Exit codes:
#   0 = clean (no conflict markers found)
#   1 = GATE_FAIL (conflict markers found; remediation hint on stderr)
#   2 = INTERNAL_ERROR (file not accessible, etc.)
#
# Pattern note: `^=======$` is matched because (a) git conflict
# separators are exactly 7 `=` chars, and (b) markdown setext heading
# underlines are typically `---` (3+ dashes), not `=======`. If a
# future entry needs setext headings, this gate would need to be
# refined — but the canonical CHANGELOG uses ATX-style headings
# (`##`, `###`) exclusively.
#
# Usage:
#   bash tools/check_no_changelog_conflict_markers.sh
#
# Invoked by:
#   - tools/wrap_push.sh (canonical, repo-tracked, all atomic commits)
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$REPO_ROOT" || { echo "INTERNAL_ERROR: cannot cd to $REPO_ROOT" >&2; exit 2; }

TARGET_FILE="docs/CHANGELOG.md"

if [ ! -f "$TARGET_FILE" ]; then
    # If the file doesn't exist, there's nothing to check.
    # Not a GATE_FAIL — just skip silently.
    echo "OK: $TARGET_FILE not present (skipping check)"
    exit 0
fi

# Match the three conflict marker patterns:
#   - `<<<<<<< ` (7 '<' + space) — opening marker
#   - `=======$` (exactly 7 '=') — separator
#   - `>>>>>>> ` (7 '>' + space) — closing marker
# Use grep -E for ERE + -c to count matches. -n for line numbers in
# the remediation hint.
if grep -nE '^(<<<<<<< |=======$|>>>>>>> )' "$TARGET_FILE" > /tmp/changelog_conflict_markers.txt 2>&1; then
    if [ -s /tmp/changelog_conflict_markers.txt ]; then
        echo "GATE_FAIL: git merge conflict markers detected in $TARGET_FILE" >&2
        echo "  offending lines:" >&2
        sed 's/^/    /' /tmp/changelog_conflict_markers.txt >&2
        echo "" >&2
        echo "  fix: manually resolve the conflict in $TARGET_FILE by" >&2
        echo "       removing the '<<<<<<<', '=======', and '>>>>>>>' lines" >&2
        echo "       (take BOTH sides if both are legitimate work, then" >&2
        echo "       verify the resulting file has no conflict markers)." >&2
        echo "  verify: grep -nE '^(<<<<<<< |=======$|>>>>>>> )' $TARGET_FILE" >&2
        echo "           should return no matches." >&2
        rm -f /tmp/changelog_conflict_markers.txt
        exit 1
    fi
fi

rm -f /tmp/changelog_conflict_markers.txt
echo "OK: no git merge conflict markers in $TARGET_FILE"
exit 0

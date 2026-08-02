#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/check_no_changelog_conflict_markers.sh
#
# Forward-only CI gate for TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX.
#
# Detects git merge conflict markers in Markdown documentation. The historical
# gate checked only docs/CHANGELOG.md, allowing the same rot in tickets,
# roadmaps, archives, and baseline proofs.
#
# This gate prevents recurrence by hard-blocking any `git push` that
# would commit unresolved `<<<<<<<`, `=======`, or `>>>>>>>` markers in docs.
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

MARKERS_FILE="$(mktemp -t chronon3d_markdown_conflict_markers.XXXXXX)"
cleanup() { rm -f "$MARKERS_FILE"; }
trap cleanup EXIT

# Scan every Markdown file under docs, including ARCHIVE and baseline proofs.
# `find` also catches a newly-created untracked .md before the clean-tree gate.
if find docs -type f -name '*.md' -print0 \
    | xargs -0 grep -nHE '^(<<<<<<< |=======$|>>>>>>> )' >"$MARKERS_FILE" 2>/dev/null; then
    if [ -s "$MARKERS_FILE" ]; then
        echo "GATE_FAIL: git merge conflict markers detected in Markdown docs" >&2
        echo "  offending lines:" >&2
        sed 's/^/    /' "$MARKERS_FILE" >&2
        echo "" >&2
        echo "  fix: resolve each listed file by removing the conflict markers." >&2
        exit 1
    fi
fi

echo "OK: no git merge conflict markers in Markdown docs (CHANGELOG + all docs/*.md)"
exit 0

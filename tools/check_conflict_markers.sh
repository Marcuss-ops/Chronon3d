#!/usr/bin/env bash
# Canonical conflict-marker gate for source code and Markdown documentation.
# One implementation owns both scopes; callers may narrow it for diagnostics.

set -euo pipefail

GATE_NAME="check_conflict_markers"
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$REPO_ROOT" || {
    echo "GATE_FAIL_INTERNAL: cannot cd to $REPO_ROOT" >&2
    exit 2
}

scope="all"
case "${1:-}" in
    ""|--scope=all) scope="all" ;;
    --scope=source) scope="source" ;;
    --scope=docs) scope="docs" ;;
    --scope)
        case "${2:-}" in
            all|source|docs) scope="$2" ;;
            *) echo "usage: $0 [--scope all|source|docs]" >&2; exit 2 ;;
        esac
        ;;
    -h|--help)
        echo "usage: $0 [--scope all|source|docs]"
        exit 0
        ;;
    *)
        echo "usage: $0 [--scope all|source|docs]" >&2
        exit 2
        ;;
esac

tmp_dir="$(mktemp -d -t chronon3d-conflict-markers.XXXXXX)"
trap 'rm -rf "$tmp_dir"' EXIT
source_hits="$tmp_dir/source.txt"
docs_hits="$tmp_dir/docs.txt"
: >"$source_hits"
: >"$docs_hits"

scan_source() {
    # Deliberately excludes .py/.md so self-test fixtures and prose do not
    # create false positives. Real merge markers always begin the line.
    git grep -nE '^(<<<<<<< |<<<<<<< HEAD|=======$|>>>>>>> )' -- \
        '*.cpp' '*.hpp' '*.h' '*.c' '*.cmake' >"$source_hits" 2>/dev/null || true
}

scan_docs() {
    [[ -d docs ]] || return 0
    # Use find rather than git grep so newly-created/untracked Markdown is
    # blocked before it can be committed. -exec ... {} + is portable across
    # GNU and BSD/macOS find/xargs environments.
    find docs -type f -name '*.md' \
        -exec grep -nHE '^(<<<<<<< |=======$|>>>>>>> )' {} + \
        >"$docs_hits" 2>/dev/null || true
}

case "$scope" in
    all) scan_source; scan_docs ;;
    source) scan_source ;;
    docs) scan_docs ;;
esac

if [[ -s "$source_hits" || -s "$docs_hits" ]]; then
    echo "GATE_FAIL: unresolved git merge conflict markers detected (scope=$scope)" >&2
    if [[ -s "$source_hits" ]]; then
        echo "  source:" >&2
        sed 's/^/    /' "$source_hits" >&2
    fi
    if [[ -s "$docs_hits" ]]; then
        echo "  docs:" >&2
        sed 's/^/    /' "$docs_hits" >&2
    fi
    echo "  fix: resolve every listed conflict block before committing." >&2
    exit 1
fi

echo "GATE_PASS: no git merge conflict markers (scope=$scope)"
echo "[INFO] ${GATE_NAME}: source and documentation conflict-marker authority is clean"
exit 0

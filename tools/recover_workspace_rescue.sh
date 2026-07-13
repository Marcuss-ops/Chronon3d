#!/usr/bin/env bash
# recover_workspace_rescue.sh -- workspace-state lost-commit rescue gate
# Closes TICKET-LOST-COMMIT-WORKSPACE-RESCUE forward-point (chore 0299a042).
# Cat-4 ancillary parallel to 5 existing Lint-checkability forward-points.
set -uo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || { echo 'not in git repo' >&2; exit 2; })"
cd "$REPO_ROOT" || { echo 'cannot cd' >&2; exit 2; }

UPSTREAM="$(git rev-parse '@{u}' 2>/dev/null || true)"
[ -z "$UPSTREAM" ] && { echo 'no upstream tracking' >&2; exit 2; }

LOCAL="$(git rev-parse HEAD)"
DIRTY="$(git status -s)"
DIVERGED=0
git merge-base --is-ancestor "$LOCAL" "$UPSTREAM" || DIVERGED=1

CHORE_FILES="$(git diff --name-only "$UPSTREAM"..."$LOCAL" 2>/dev/null || echo '')"

if [ "$DIVERGED" -eq 0 ] && [ -z "$DIRTY" ]; then
    echo 'OK: workspace clean and local HEAD is ancestor of upstream'
    exit 0
fi

echo '' >&2
echo 'GATE_FAIL: workspace requires rescue' >&2
echo "  local HEAD = $LOCAL" >&2
echo "  upstream   = $UPSTREAM" >&2
echo "  diverged = $DIVERGED  dirty = $([ -n "$DIRTY" ] && echo yes || echo no)" >&2

if [ "${CHRONON3D_RESCUE_AUTO:-0}" = "1" ]; then
    if [ -n "$DIRTY" ]; then
        echo 'CHRONON3D_RESCUE_AUTO=1 + dirty workspace = INTERNAL_ERROR (refusing destructive reset)' >&2
        exit 2
    fi
    if [ -z "$CHORE_FILES" ]; then
        echo 'no UNIQUE files detected (semantic-identical to upstream); surface-only path' >&2
        echo 'manual: git reset --hard @{u} + re-author content' >&2
        exit 1
    fi
    SNAPSHOT_DIR="/tmp/chronon3d-rescue-snapshot/$LOCAL"
    mkdir -p "$SNAPSHOT_DIR"
    git format-patch -1 "$LOCAL" --stdout > "$SNAPSHOT_DIR/pre-reset.patch" 2>/dev/null || true
    git reset --hard "$UPSTREAM"
    # shellcheck disable=SC2086
    git checkout "$LOCAL" -- $CHORE_FILES
    echo 'AUTO-RECOVERY: UNIQUE files restored atop upstream; ready for fresh commit + push' >&2
    exit 0
fi

echo '' >&2
echo 'Manual Rescue Instructions (CHRONON3D_RESCUE_AUTO=1 to enable semi-auto):' >&2
cat <<'INSTRUCTIONS' >&2
  Pattern 1 / 2 (divergence detected):
    CHORE=$(git rev-parse HEAD)
    git reset --hard '@{u}'
    git checkout $CHORE -- <unique_files>
    git status -s (verify), git add, git commit, tools/wrap_push.sh origin main
INSTRUCTIONS
[ -n "$DIRTY" ] && echo 'Workspace DIRTY: commit or stash first.' >&2
exit 1

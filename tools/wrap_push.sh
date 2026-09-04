#!/usr/bin/env bash
# wrap_push.sh — GATE-MNT-01 portable pre-push wrapper.
#
# Flow:
#   1. fetch and fast-forward the target branch when safe;
#   2. enforce check_main_clean.sh;
#   3. run the developer chain through tools/run_developer_gates.sh;
#   4. with CHRONON3D_GATE_PROFILE=wbh, run WBH_ONLY_GATES;
#   5. push and verify the SHA-triple invariant.
#
# Gate membership lives only in tools/gates/manifest.sh. Do not duplicate
# individual developer or WBH gate names in this wrapper.
#
# Usage:
#   tools/wrap_push.sh origin main
#   tools/wrap_push.sh

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
SCRIPT_DIR="${REPO_ROOT}/tools"
GATE="${SCRIPT_DIR}/check_main_clean.sh"

# shellcheck source=gates/manifest.sh
source "${SCRIPT_DIR}/gates/manifest.sh"

# developer = fast local chain; wbh = developer chain + WBH_ONLY_GATES.
GATE_PROFILE="${CHRONON3D_GATE_PROFILE:-developer}"
readonly GATE_PROFILE

if [ ! -x "$GATE" ]; then
    echo "wrap_push.sh: gate script missing or not executable: $GATE" >&2
    echo "  fix: chmod +x tools/check_main_clean.sh" >&2
    exit 2
fi

# Parse the first two positional git-push arguments as remote and branch.
TARGET_REMOTE="origin"
TARGET_BRANCH="$(git rev-parse --abbrev-ref HEAD)"
POSITIONAL_INDEX=0
for arg in "$@"; do
    case "$arg" in
        --*) ;;
        *)
            case $POSITIONAL_INDEX in
                0) TARGET_REMOTE="$arg" ;;
                1) TARGET_BRANCH="$arg" ;;
            esac
            POSITIONAL_INDEX=$((POSITIONAL_INDEX + 1))
            ;;
    esac
done

REMOTE_REF="${TARGET_REMOTE}/${TARGET_BRANCH}"

if ! git fetch "$TARGET_REMOTE" 2>/dev/null; then
    echo "wrap_push.sh: GATE_FAIL: git fetch $TARGET_REMOTE failed" >&2
    echo "  fix: verify network/auth/remote config, then retry" >&2
    echo "GATE_FAIL"
    exit 1
fi

# Repair an unset per-branch rebase preference; never override an explicit value.
if ! git config --local --get branch."$TARGET_BRANCH".rebase 2>/dev/null >/dev/null; then
    echo "wrap_push.sh: GATE-MNT-01-EXT auto-repair: setting branch.${TARGET_BRANCH}.rebase=true (was unset)"
    git config branch."$TARGET_BRANCH".rebase true
fi

# Auto fast-forward when the remote is a strict descendant of local HEAD.
LOCAL_REF="$(git rev-parse HEAD)"
REMOTE_COMMIT="$(git rev-parse "$REMOTE_REF" 2>/dev/null || echo "")"

AUTO_FF_SETTING="${CHRONON3D_WRAP_PUSH_AUTO_FF:-true}"
if [ "$AUTO_FF_SETTING" != "true" ]; then
    echo "wrap_push.sh: auto-FF skipped (CHRONON3D_WRAP_PUSH_AUTO_FF=${AUTO_FF_SETTING})"
fi
if [ "$AUTO_FF_SETTING" = "true" ] && [ -n "$REMOTE_COMMIT" ] \
   && [ "$LOCAL_REF" != "$REMOTE_COMMIT" ] \
   && git merge-base --is-ancestor "$LOCAL_REF" "$REMOTE_COMMIT"; then
    echo "wrap_push.sh: auto-FF: $REMOTE_REF is fast-forward of HEAD — merging"
    if ! git merge --ff-only "$REMOTE_REF"; then
        echo "" >&2
        echo "GATE_FAIL: remote is ahead but fast-forward not possible" >&2
        echo "  local  = $LOCAL_REF" >&2
        echo "  remote = $REMOTE_COMMIT" >&2
        echo "  fix: divergence requires manual intervention" >&2
        echo "    git pull --rebase $TARGET_REMOTE $TARGET_BRANCH" >&2
        echo "    OR: $TARGET_REMOTE fetch + manual merge" >&2
        echo "GATE_FAIL"
        exit 1
    fi
    echo "wrap_push.sh: auto-FF: merged remote commits into local"
fi

# main is the integration line: force-pushes are never accepted here.
if [[ "$TARGET_BRANCH" == "main" ]]; then
    for arg in "$@"; do
        case "$arg" in
            --force|--force-with-lease|-f)
                echo "wrap_push.sh: GATE_FAIL: force-push is forbidden on main" >&2
                echo "  fix: reconcile origin/main, run gates, then push fast-forward only" >&2
                echo "GATE_FAIL"
                exit 1
                ;;
        esac
    done
fi

# Release/WBH callers may pin the exact certified SHA; otherwise use current HEAD.
TESTED_SHA="${CHRONON3D_TESTED_SHA:-$(git rev-parse HEAD)}"
if ! git cat-file -e "${TESTED_SHA}^{commit}" 2>/dev/null; then
    echo "wrap_push.sh: GATE_FAIL: CHRONON3D_TESTED_SHA is not a commit: $TESTED_SHA" >&2
    echo "GATE_FAIL"
    exit 1
fi

# Canonical clean-main gate.
echo "wrap_push.sh: GATE-MNT-01 pre-flight (tested_sha=$TESTED_SHA)"
if ! "$GATE"; then
    echo "wrap_push.sh: gate FAILED — push aborted" >&2
    echo "  fix: ensure working tree clean (post-FF), then retry" >&2
    echo "  note: pre-push auto-FF has been applied; if the gate still rejects" >&2
    echo "        afterwards, the divergence is non-FF-able and manual rebase" >&2
    echo "        is required (see gate diagnostics above)." >&2
    exit 1
fi

# Developer gate membership is resolved by run_developer_gates.sh from the manifest.
echo "wrap_push.sh: running developer gate chain (via run_developer_gates.sh ${TARGET_REMOTE} ${TARGET_BRANCH})..."
bash "${SCRIPT_DIR}/run_developer_gates.sh" "${TARGET_REMOTE}" "${TARGET_BRANCH}" \
    || { echo "wrap_push.sh: GATE_FAIL on run_developer_gates.sh (exit $?)" >&2; exit 1; }

# WBH-only gate membership is resolved from the same manifest.
if [[ "$GATE_PROFILE" == "wbh" ]]; then
    for gate in "${WBH_ONLY_GATES[@]}"; do
        echo "wrap_push.sh: running WBH gate: ${gate}"
        bash "${SCRIPT_DIR}/${gate}" \
            || { echo "wrap_push.sh: GATE_FAIL on ${gate} (exit $?)" >&2; exit 1; }
    done
else
    echo "wrap_push.sh: GATE_PROFILE=${GATE_PROFILE} — skipping WBH_ONLY_GATES from canonical manifest"
fi

# The commit tested by the gates must still be HEAD immediately before push.
LOCAL_SHA_PRE_PUSH="$(git rev-parse HEAD)"
if [[ "$LOCAL_SHA_PRE_PUSH" != "$TESTED_SHA" ]]; then
    echo "wrap_push.sh: GATE_FAIL: HEAD changed after tests; tested SHA is stale" >&2
    echo "  tested_sha = $TESTED_SHA" >&2
    echo "  local_head = $LOCAL_SHA_PRE_PUSH" >&2
    echo "  fix: rerun the certification gates on current HEAD" >&2
    echo "GATE_FAIL"
    exit 1
fi
echo "wrap_push.sh: LOCAL_SHA_PRE_PUSH=$LOCAL_SHA_PRE_PUSH — invoking: git push $*"

git push --no-verify "$@"
PUSH_RC=$?
if [ "$PUSH_RC" -ne 0 ]; then
    echo "wrap_push.sh: GATE_FAIL: git push exited $PUSH_RC — push aborted" >&2
    echo "GATE_FAIL"
    exit 1
fi

POSTPUSH_SHA="$(git rev-parse HEAD)"
if ! git rev-parse '@{u}' >/dev/null 2>&1; then
    echo "wrap_push.sh: GATE_INTERNAL_ERROR: post-push @{u} resolution failed" >&2
    echo "  fix: verify remote tracking: git branch --set-upstream-to=$TARGET_REMOTE/$TARGET_BRANCH $TARGET_BRANCH" >&2
    echo "GATE_FAIL"
    exit 2
fi
UPSTREAM_SHA="$(git rev-parse '@{u}')"

if [ "$POSTPUSH_SHA" != "$UPSTREAM_SHA" ] || [ "$POSTPUSH_SHA" != "$TESTED_SHA" ]; then
    echo "wrap_push.sh: GATE_FAIL: post-push SHA mismatch — tested SHA did not become origin/main" >&2
    echo "  pre_push_SHA   = $LOCAL_SHA_PRE_PUSH" >&2
    echo "  post_push_SHA  = $POSTPUSH_SHA" >&2
    echo "  upstream_SHA   = $UPSTREAM_SHA" >&2
    echo "  tested_SHA     = $TESTED_SHA" >&2
    echo "  fix: chore <$LOCAL_SHA_PRE_PUSH> may be lost between local and upstream (race window)." >&2
    echo "  fix: see AGENTS.md §Post-push SHA-selfcheck invariant '21ece2b3 unique-edit recovery variant' for the reset+re-apply template." >&2
    echo "GATE_FAIL"
    exit 1
fi

echo "wrap_push.sh: post-push SHA-triple VERIFIED — tested $TESTED_SHA == HEAD $POSTPUSH_SHA == origin/$TARGET_BRANCH $UPSTREAM_SHA == @{u}"
exit 0

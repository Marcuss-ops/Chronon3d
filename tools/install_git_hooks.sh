#!/usr/bin/env bash
# install_git_hooks.sh — configure git to use the tracked .githooks/ directory.
#
# Sets core.hooksPath to .githooks (relative to repo root), enabling the
# tracked commit-msg, pre-commit, and pre-push hooks for every clone.
# Idempotent — safe to run multiple times.
#
# Usage:
#   bash tools/install_git_hooks.sh
# ═══════════════════════════════════════════════════════════════════════════

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"

echo "install_git_hooks.sh: setting core.hooksPath = .githooks"
git config --local core.hooksPath .githooks

echo "install_git_hooks.sh: verifying..."
CURRENT="$(git config --local --get core.hooksPath || echo 'UNSET')"
echo "  core.hooksPath = ${CURRENT}"

if [ "${CURRENT}" != ".githooks" ]; then
    echo "install_git_hooks.sh: FAILED — hooksPath is '${CURRENT}', expected '.githooks'" >&2
    exit 1
fi

# Verify the hook files exist and are executable.
HOOKS_DIR="${REPO_ROOT}/.githooks"
for hook in commit-msg pre-commit pre-push; do
    if [ ! -x "${HOOKS_DIR}/${hook}" ]; then
        echo "install_git_hooks.sh: WARNING — ${hook} is not executable, fixing..."
        chmod +x "${HOOKS_DIR}/${hook}"
    fi
done

echo "install_git_hooks.sh: hooks installed and verified (commit-msg, pre-commit, pre-push)"
exit 0

#!/usr/bin/env bash
set -euo pipefail

GATE_NAME=check_no_modular_graph
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$REPO_ROOT"

if matches=$(rg -n "use_modular_graph" include src apps tests); then
    echo "GATE_FAIL: retired use_modular_graph symbol remains in the active source surface"
    echo "$matches"
    exit 1
fi

echo "GATE_PASS: retired use_modular_graph symbol absent from include/src/apps/tests"
echo "[INFO] ${GATE_NAME}: legacy non-modular render path is not selectable"

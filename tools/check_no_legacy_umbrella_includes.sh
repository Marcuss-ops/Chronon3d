#!/usr/bin/env bash
set -euo pipefail

GATE_NAME=check_no_legacy_umbrella_includes
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

matches=$(rg -n '#include <chronon3d/(chronon3d|runtime|internal)\.hpp>' \
  "$repo_root/include" "$repo_root/src" "$repo_root/tests" "$repo_root/apps" || true)

if [[ -n "$matches" ]]; then
  echo "GATE_FAIL: legacy Chronon3D umbrella headers are still included"
  echo "$matches"
  exit 1
fi

echo "GATE_PASS: legacy umbrella headers are not included by active source"
echo "[INFO] ${GATE_NAME}: supported authoring, render and advanced surfaces remain canonical"

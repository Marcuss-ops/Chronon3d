#!/usr/bin/env bash
set -euo pipefail

GATE_NAME=check_no_preset_catalog_magic_statics
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

matches=$(rg -n -U \
  'motion_preset_catalog\([^)]*\)\s*\{\s*static\s+const|builtin_text_preset_registry\([^)]*\)\s*\{\s*static\s+const' \
  "$repo_root/include" "$repo_root/src" || true)

if [[ -n "$matches" ]]; then
  echo "GATE_FAIL: preset catalog accessors contain magic-static state"
  echo "$matches"
  exit 1
fi

echo "GATE_PASS: preset catalog accessors contain no magic-static state"
echo "[INFO] ${GATE_NAME}: built-in preset storage is explicit and immutable"

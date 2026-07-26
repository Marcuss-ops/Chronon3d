#!/usr/bin/env bash
set -euo pipefail

GATE_NAME=check_no_software_effect_dispatch_switch
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source_file="$repo_root/src/backends/software/utils/effects/effect_stack.cpp"

if rg -n 'switch\s*\([^)]*effect_type|switch\s*\([^)]*param_type' "$source_file"; then
  echo "GATE_FAIL: software effect stack still contains a parallel type dispatcher"
  exit 1
fi

echo "GATE_PASS: software effect stack delegates through SoftwareRegistry"
echo "[INFO] ${GATE_NAME}: no effect-type switch remains in the production dispatcher"

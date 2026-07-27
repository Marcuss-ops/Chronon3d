#!/usr/bin/env bash
set -euo pipefail

GATE_NAME=check_effect_subsystem_ownership
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

matches=$(rg -n --hidden \
  --glob '!build/**' \
  --glob '!docs/**' \
  'backends/software/utils/effects|utils/effects/(effect_|glow_)' \
  "$repo_root/include" "$repo_root/src" "$repo_root/tests" "$repo_root/apps" || true)

if [[ -n "$matches" ]]; then
  echo "GATE_FAIL: software effect implementation still belongs to utils:"
  echo "$matches"
  exit 1
fi

test -d "$repo_root/src/backends/software/effects"
test -f "$repo_root/src/backends/software/effects/CMakeLists.txt"
echo "GATE_PASS: software effect algorithms belong to the dedicated effects module"
echo "[INFO] ${GATE_NAME}: utils contains no effect implementation ownership"

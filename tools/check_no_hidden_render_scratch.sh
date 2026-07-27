#!/usr/bin/env bash
set -euo pipefail

GATE_NAME=check_no_hidden_render_scratch
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

matches=$(rg -n --hidden --glob '!build/**' \
  'static[[:space:]]+thread_local[[:space:]]+std::vector' \
  "$repo_root/src/backends/text" \
  "$repo_root/src/backends/software" \
  "$repo_root/src/scene" || true)

if [[ -n "$matches" ]]; then
  echo "GATE_FAIL: hidden render scratch remains:" >&2
  echo "$matches" >&2
  exit 1
fi

echo "GATE_PASS: render scratch is not hidden in text, software, or scene code"
echo "[INFO] ${GATE_NAME}: no thread-local vector scratch remains in active render paths"

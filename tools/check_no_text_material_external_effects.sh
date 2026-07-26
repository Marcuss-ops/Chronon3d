#!/usr/bin/env bash
set -euo pipefail

GATE_NAME=check_no_text_material_external_effects
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

matches=$(rg -n -P \
  'use_material_(glow|shadow)|\bglow_(radius|intensity|color)\b|(?<!inner_)\bshadow_(offset|blur|opacity|color)\b' \
  "$repo_root/include/chronon3d/text/text_material.hpp" \
  "$repo_root/include/chronon3d/authoring/material.hpp" \
  "$repo_root/src/text" | rg -v 'bevel_shadow_opacity|f32 shadow_opacity' || true)

if [[ -n "$matches" ]]; then
  echo "GATE_FAIL: TextMaterial still exposes external glow/shadow state"
  echo "$matches"
  exit 1
fi

echo "GATE_PASS: TextMaterial contains no external glow/shadow state"
echo "[INFO] ${GATE_NAME}: external glow and drop shadow remain EffectStack concerns"

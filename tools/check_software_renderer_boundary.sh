#!/usr/bin/env bash
# ============================================================================
# tools/check_software_renderer_boundary.sh
#
# 06 R5 — Gate di confine per SoftwareRenderer (subset eseguibile del gate
# R5 del piano 06). Verifica le seguenti *invarianti minime*, da
# rilassare durante le PR R1..R4 e da rendere bloccanti a chiusura R5:
#
#   I1. SoftwareRenderer NON eredita `graph::RenderBackend` (R3b).
#   I2. Header LOC <= 200 (R4).
#   I3. Non-local includes (#include <chronon3d/...>) <= 6 (R4).
#   I4. Nessuna `dynamic_cast<SoftwareRenderer*>` in src/ (R3b).
#   I5. Nessuna `SoftwareRenderer&` nelle superfici *di processo*
#       (processor + render_graph helper).
#
# Exit code 0 se tutte le invarianti passano; 1 altrimenti. Lo script
# NON solleva guard di build: deve passare anche in CI no-content
# (CHRONON3D_BUILD_CONTENT=OFF).
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

HDR="include/chronon3d/backends/software/software_renderer.hpp"
MAX_HDR_LOC=200
MAX_NON_LOCAL_INC=6

errs=0
fail() { echo "[FAIL] $1" >&2; errs=$((errs + 1)); }
ok()   { echo "[ OK ] $1"; }

# -- I1: doppia eredità rimossa --
dual_match=""
if [[ -f "$HDR" ]]; then
  dual_match="$(grep -E 'class SoftwareRenderer\b.*public.*Renderer.*public.*(graph::)?RenderBackend|class SoftwareRenderer\b.*public.*(graph::)?RenderBackend.*public.*Renderer' "$HDR" 2>/dev/null || true)"
fi
if [[ -n "$dual_match" ]]; then
  fail "I1: SoftwareRenderer ancora in doppia identità (renderer + backend)"
  echo "      $dual_match" >&2
else
  ok "I1: SoftwareRenderer single identity (o header assente — R3b dipende da R1)"
fi

# -- I2: header LOC --
if [[ -f "$HDR" ]]; then
  loc=$(wc -l < "$HDR")
  if [[ "$loc" -gt "$MAX_HDR_LOC" ]]; then
    fail "I2: header LOC=$loc > $MAX_HDR_LOC (target R4)"
  else
    ok "I2: header LOC=$loc <= $MAX_HDR_LOC"
  fi
fi

# -- I3: non-local includes --
if [[ -f "$HDR" ]]; then
  nli=$(grep -E '^#include <(chronon3d|backends)/' "$HDR" 2>/dev/null | wc -l)
  if [[ "$nli" -gt "$MAX_NON_LOCAL_INC" ]]; then
    fail "I3: non-local includes=$nli > $MAX_NON_LOCAL_INC (target R4)"
  else
    ok "I3: non-local includes=$nli <= $MAX_NON_LOCAL_INC"
  fi
fi

# -- I4: nessuna dynamic_cast<SoftwareRenderer*>
casts=$(grep -RIn 'dynamic_cast<SoftwareRenderer' \
  src/ include/chronon3d/ apps/ 2>/dev/null | wc -l || true)
if [[ "$casts" -ne 0 ]]; then
  fail "I4: $casts dynamic_cast<SoftwareRenderer*> ancora presenti"
  grep -RIn 'dynamic_cast<SoftwareRenderer' src/ include/chronon3d/ apps/ 2>/dev/null \
    | sed 's|^|      |' >&2
else
  ok "I4: nessuna dynamic_cast<SoftwareRenderer*>"
fi

# -- I5: SoftwareRenderer& nelle superfici di processo (R2)
proc_uses=$(grep -RIn 'SoftwareRenderer&' \
  src/backends/software/processors/ \
  src/backends/software/text_run_processor.cpp \
  src/render_graph/ \
  src/runtime/ \
  include/chronon3d/backends/ \
  2>/dev/null | wc -l || true)
if [[ "$proc_uses" -ne 0 ]]; then
  fail "I5: $proc_uses riferimenti SoftwareRenderer& nelle superfici di processo (target R2)"
  grep -RIn 'SoftwareRenderer&' \
    src/backends/software/processors/ \
    src/backends/software/text_run_processor.cpp \
    src/render_graph/ \
    src/runtime/ \
    include/chronon3d/backends/ \
    2>/dev/null | sed 's|^|      |' >&2
else
  ok "I5: nessun SoftwareRenderer& nelle superfici di processo"
fi

echo
if [[ "$errs" -ne 0 ]]; then
  echo "FAILED: $errs boundary violation(s)" >&2
  exit 1
fi
echo "OK: software renderer boundary invariants hold (06 R5 gate)"
exit 0

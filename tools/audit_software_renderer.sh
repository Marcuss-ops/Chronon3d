#!/usr/bin/env bash
# ============================================================================
# tools/audit_software_renderer.sh
#
# 06 R1 — Inventario canonico di `SoftwareRenderer`.
# Classifica i metodi pubblici, conta gli include, gli include non-locali,
# i forwarder a m_runtime->backend() (overhead di doppio rimbalzo), i
# dynamic_cast<SoftwareRenderer*>, gli usi di `SoftwareRenderer&` nei
# processor/render_graph, e la presenza di doppia eredità
# Renderer/RenderBackend.
#
# Usage:
#   tools/audit_software_renderer.sh [output.md]
#
# Default output: docs/audits/2026-06-software-renderer-inventory.md
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# SoftwareRenderer is intentionally NOT on the public API surface (P1-E Pass E).
# The header lives under src/backends/software/include_private/ since commit
# 2c05d0d4 ("refactor(sdk): move software internal headers into
# src/backends/software/include_private/"). We accept BOTH locations to (a)
# honour the current private layout and (b) keep the gate functional when
# bisecting historical commits where the header was still under include/.
HDR_PRIVATE="${ROOT}/src/backends/software/include_private/chronon3d/backends/software/software_renderer.hpp"
HDR_PUBLIC="${ROOT}/include/chronon3d/backends/software/software_renderer.hpp"
if [[ -f "$HDR_PRIVATE" ]]; then
  HDR="$HDR_PRIVATE"
elif [[ -f "$HDR_PUBLIC" ]]; then
  HDR="$HDR_PUBLIC"
else
  echo "FATAL: missing software_renderer.hpp (checked $HDR_PRIVATE and $HDR_PUBLIC)" >&2
  exit 2
fi
CPP="${ROOT}/src/backends/software/software_renderer.cpp"
OUT="${1:-${ROOT}/docs/audits/2026-06-software-renderer-inventory.md}"
if [[ ! -f "$CPP" ]]; then
  echo "FATAL: missing $CPP" >&2
  exit 2
fi

mkdir -p "$(dirname "$OUT")"

# 1. Metriche di base.
HDR_LOC=$(wc -l < "$HDR")
NON_LOCAL_INC=$(grep -E '^#include <(chronon3d|backends)/' "$HDR" 2>/dev/null | wc -l || true)
LOCAL_INC=$(grep -E '^#include "[^"]+"' "$HDR" 2>/dev/null | wc -l || true)
GUARD_DEFS=$(grep -E '^#ifdef|^#if defined' "$CPP" 2>/dev/null | wc -l || true)

# 2. Doppio rimbalzo: metodi del renderer che inoltrano a m_runtime->backend()
# Sono candidati R3a per essere rimossi dal renderer.
FWD_BACKEND=$(grep -E 'm_runtime->backend\(\)\.' "$CPP" 2>/dev/null | wc -l || true)
FWD_RUNTIME=$(grep -E 'm_runtime->' "$CPP" 2>/dev/null | wc -l || true)

# 3. Doppia identità: SoftwareRenderer : public ... , public ... RenderBackend
DUAL_INH=$(grep -nE 'class SoftwareRenderer\b.*:.*public\b' "$HDR" || true)

# 4. dynamic_cast<SoftwareRenderer*> e static_cast in pipeline/processor
CASTS=$(grep -RIn 'dynamic_cast<SoftwareRenderer' src/ include/ apps/ 2>/dev/null | wc -l || true)
STATIC_CASTS=$(grep -RIn 'static_cast<SoftwareRenderer' src/ include/ apps/ 2>/dev/null | wc -l || true)

# 5. SoftwareRenderer& nei processor / header di processo.
PROC_USES=$(grep -RIn 'SoftwareRenderer&' \
  src/backends/software/ \
  src/render_graph/ \
  src/runtime/ \
  include/chronon3d/backends/software/ \
  2>/dev/null | wc -l || true)

# 6. Metodi pubblici (euristica): righe di dichiarazione dentro il blocco public.
awk '
  BEGIN { p=0 }
/^public:/ { p=1; next }
/^(private|protected):/ { p=0; next }
  p && /^[[:space:]]+[A-Za-z_~][A-Za-z0-9_:<>,\*& ]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\(/ {
    sub(/^[[:space:]]+/, "")
    print
  }
' "$HDR" | sort -u > /tmp/sw_public_methods.txt
PUB_COUNT=$(wc -l < /tmp/sw_public_methods.txt)

# 7. RenderBackend override implementati nel renderer (candidati R3a per rimozione)
RB_OVERRIDES=$(grep -E 'override' "$HDR" 2>/dev/null | wc -l || true)

# 8. Costruzione del report.
{
  echo "# SoftwareRenderer — Audit (R1)"
  echo
  echo "Generato da \`tools/audit_software_renderer.sh\` su commit \`$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo '<n/a>')\`."
  echo
  echo "## Metriche di base"
  echo
  echo "| Metrica | Valore | Target R4 / R3 |"
  echo "|---|---:|---|"
  echo "| Header LOC (\`software_renderer.hpp\`) | ${HDR_LOC} | ≤ 200 |"
  echo "| \\#include \\<chronon3d/\\> non-locali nell'header | ${NON_LOCAL_INC} | ≤ 6 |"
  echo "| \\#include \"...\" locali nell'header | ${LOCAL_INC} | n/a |"
  echo "| Blocchi \`#ifdef\`/\`#if defined\` nel cpp | ${GUARD_DEFS} | n/a |"
  echo "| Metodi pubblici totali | ${PUB_COUNT} | n/a |"
  echo "| Override \`override\` (candidati R3a) | ${RB_OVERRIDES} | 0 al termine di R3 |"
  echo "| Inoltri puri \`m_runtime->backend().\` (R3a) | ${FWD_BACKEND} | 0 al termine di R3 |"
  echo "| Inoltri \`m_runtime->\` totali (forwarders) | ${FWD_RUNTIME} | n/a |"
  echo
  echo "## Doppia identità renderer/backend"
  echo
  echo '```cpp'
  if [[ -n "$DUAL_INH" ]]; then
    echo "$DUAL_INH"
  else
    echo "// non trovata (bene: singola eredità)"
  fi
  echo '```'
  echo
  echo "## dynamic_cast / static_cast verso SoftwareRenderer"
  echo
  echo "Trovati \`dynamic_cast<SoftwareDesigner*>\` = **${CASTS}**, \`static_cast<SoftwareRenderer*>\` = **${STATIC_CASTS}**."
  echo
  if [[ "$CASTS" -gt 0 ]]; then
    echo "Dettaglio \`dynamic_cast\`:"
    echo
    echo '```text'
    grep -RIn 'dynamic_cast<SoftwareRenderer' src/ include/ apps/ 2>/dev/null | sed 's|^|  |'
    echo '```'
  fi
  echo
  echo "## SoftwareRenderer& nelle superfici dei processor / runtime"
  echo
  echo "Totale grezzo in processori e render_graph: **${PROC_USES}**."
  echo
  if [[ "$PROC_USES" -gt 0 ]]; then
    echo "Dettaglio (prime 40 occorrenze):"
    echo
    echo '```text'
    grep -RIn 'SoftwareRenderer&' \
      src/backends/software/ \
      src/render_graph/ \
      src/runtime/ \
      include/chronon3d/backends/software/ \
      2>/dev/null | head -40 | sed 's|^|  |'
    echo '```'
  fi
  echo
  echo "## Metodi pubblici dell'header (euristica)"
  echo
  echo '```text'
  cat /tmp/sw_public_methods.txt | sed 's|^|  |'
  echo '```'
  echo
  echo "## Classificazione per categoria"
  echo
  echo "Da compilare in PR R1.1 — colonne previste: backend / orchestrazione /"
  echo "settings / telemetry / cache / servizio."
} > "$OUT"

echo "Report written: $OUT"
echo "Header LOC: $HDR_LOC | non-local includes: $NON_LOCAL_INC | renderer& in proc: $PROC_USES | dynamic_cast: $CASTS"

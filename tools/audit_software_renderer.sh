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
HDR="${ROOT}/include/chronon3d/backends/software/software_renderer.hpp"
CPP="${ROOT}/src/backends/software/software_renderer.cpp"
OUT="${1:-${ROOT}/docs/audits/2026-06-software-renderer-inventory.md}"

if [[ ! -f "$HDR" ]]; then
  echo "FATAL: missing $HDR" >&2
  exit 2
fi
if [[ ! -f "$CPP" ]]; then
  echo "FATAL: missing $CPP" >&2
  exit 2
fi

mkdir -p "$(dirname "$OUT")"

# Helper: count matches safely under set -euo pipefail.
# grep -c treats "no matches" as exit 0 with output "0", avoiding
# the pipefail trap where `grep ... | wc -l` kills the script on zero hits.
count_matches() {
    local pattern="$1"
    local file="$2"
    grep -Ec "$pattern" "$file" 2>/dev/null || true
}

count_matches_recursive() {
    local pattern="$1"
    shift
    grep -RIc "$pattern" "$@" 2>/dev/null | awk -F: '{s+=$NF} END{print s+0}'
}

# 1. Metriche di base.
HDR_LOC=$(wc -l < "$HDR")
NON_LOCAL_INC=$(count_matches '^#include <(chronon3d|backends)/' "$HDR")
LOCAL_INC=$(count_matches '^#include "[^"]+"' "$HDR")
GUARD_DEFS=$(count_matches '^#ifdef|^#if defined' "$CPP")

# 2. Doppio rimbalzo: metodi del renderer che inoltrano a m_runtime->backend()
# Sono candidati R3a per essere rimossi dal renderer.
FWD_BACKEND=$(count_matches 'm_runtime->backend\(\)\.' "$CPP")
FWD_RUNTIME=$(count_matches 'm_runtime->' "$CPP")

# 3. Doppia identità: SoftwareRenderer : public ... , public ... RenderBackend
DUAL_INH=$(grep -nE 'class SoftwareRenderer\b.*:.*public\b' "$HDR" || true)

# 4. dynamic_cast<SoftwareRenderer*> e static_cast in pipeline/processor
CASTS=$(count_matches_recursive 'dynamic_cast<SoftwareRenderer' src/ include/ apps/)
STATIC_CASTS=$(count_matches_recursive 'static_cast<SoftwareRenderer' src/ include/ apps/)

# 5. SoftwareRenderer& nei processor / header di processo.
PROC_USES=$(count_matches_recursive 'SoftwareRenderer&' \
  src/backends/software/ \
  src/render_graph/ \
  src/runtime/ \
  include/chronon3d/backends/software/)

# 6. Metodi pubblici (euristica): righe di dichiarazione dentro il blocco public.
# Use mktemp + trap to avoid collision between concurrent CI jobs.
SW_PUBLIC_TMP="$(mktemp -t sw_public_methods.XXXXXX.txt)"
trap 'rm -f "$SW_PUBLIC_TMP"' EXIT

awk '
  BEGIN { p=0 }
/^public:/ { p=1; next }
/^(private|protected):/ { p=0; next }
  p && /^[[:space:]]+[A-Za-z_~][A-Za-z0-9_:<>,\*& ]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\(/ {
    sub(/^[[:space:]]+/, "")
    print
  }
' "$HDR" | sort -u > "$SW_PUBLIC_TMP"
PUB_COUNT=$(wc -l < "$SW_PUBLIC_TMP")

# 7. RenderBackend override implementati nel renderer (candidati R3a per rimozione)
RB_OVERRIDES=$(grep -E 'override' "$HDR" 2>/dev/null | wc -l)

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
  echo "Trovati \`dynamic_cast<SoftwareRenderer*>\` = **${CASTS}**, \`static_cast<SoftwareRenderer*>\` = **${STATIC_CASTS}**."
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
  cat "$SW_PUBLIC_TMP" | sed 's|^|  |'
  echo '```'
  echo
  echo "## Classificazione per categoria"
  echo
  echo "Da compilare in PR R1.1 — colonne previste: backend / orchestrazione /"
  echo "settings / telemetry / cache / servizio."
} > "$OUT"

echo "Report written: $OUT"
echo "Header LOC: $HDR_LOC | non-local includes: $NON_LOCAL_INC | renderer& in proc: $PROC_USES | dynamic_cast: $CASTS"

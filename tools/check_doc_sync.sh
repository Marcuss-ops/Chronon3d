#!/usr/bin/env bash
# ============================================================================
# tools/check_doc_sync.sh
#
# 07 D3 — Gate di co-update per STATUS.md / ROADMAP.md / NEXT_STEPS.md /
# CHANGELOG.md, ADR e file refactor-roadmap*.
#
# Regole (matrice del piano 07 sez. D3):
#
#   1. Qualsiasi cambio in src/runtime/**       ⇒ STATUS.md obbligatorio
#      e NEXT_STEPS.md obbligatorio.
#   2. Qualsiasi cambio in include/chronon3d/**  ⇒ almeno un ADR in
#      docs/adr/ deve esistere per l'area (warning finché lo slot
#      non è *Accepted*).
#   3. Qualsiasi cambio in src/render_graph/** o
#      include/chronon3d/render_graph/**  ⇒ refactor-roadmap/01..05
#      toccato oppure ADR-001/002 esistente.
#   4. Qualsiasi cambio in vcpkg.json o
#      CMakePresets.json                       ⇒
#      docs/stabilization-plan/08-dependency-profiles.md toccato.
#   5. Chiusura di un TICKET-NNN (parola "Closes #NNN" o
#      "TICKET-NNN: done" nel messaggio) ⇒ CHANGELOG.md toccato.
#
# Usage:
#   tools/check_doc_sync.sh [--wip] [--strict] [<base_ref>]
#
# <base_ref> default: HEAD~1 (ultimo commit). Se viene passato --all,
# confronta HEAD contro il primo commit del branch corrente.
# --wip  : logga warning, exit 0.
# --strict: exit 1 su warning e violazione (default per CI).
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

wip=0
strict=1
diff_range="HEAD~1..HEAD"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --wip)   wip=1;  shift ;;
    --strict) strict=1; shift ;;
    --all)   diff_range="$(git rev-list --max-parents=0 HEAD)..HEAD"; shift ;;
    -*) echo "Unknown flag: $1" >&2; exit 2 ;;
    *)
      if [[ -z "${base:-}" ]]; then base="$1"; else echo "extra arg $1" >&2; exit 2; fi
      shift ;;
  esac
done
[[ -n "${base:-}" ]] && diff_range="${base}..HEAD"

# Ottieni i file modificati (commited + working) rispetto a diff_range.
changed_files="$(git diff --name-only "$diff_range" 2>/dev/null || true)"
[[ -z "$changed_files" ]] && {
  echo "OK: no diff in range ${diff_range}"
  exit 0
}

errs=0
warn=0
fail() { echo "[FAIL] $1" >&2; errs=$((errs + 1)); }
note() { echo "[FAIL] $1" >&2; errs=$((errs + 1)); }
warnf() { echo "[WARN] $1" >&2; warn=$((warn + 1)); }
okf()   { echo "[ OK ] $1"; }

# -- helpers -----------------------------------------------------------------------------
has_change() {
  local pattern="$1"
  printf '%s\n' "$changed_files" | grep -Eq "$pattern"
}

# Se wip=1, i WARNING diventano OK; i FAIL restano FAIL.
apply_lvl() {
  # $1: livello del problema (fail|warn)
  # Silent: restituisce 1 se va contato come errore.
  [[ "$1" == "fail" ]] && return 0
  if [[ "$wip" -eq 1 ]]; then return 1; fi
  return 0
}

# -- R1: src/runtime/** ⇒ STATUS.md + NEXT_STEPS.md -----------------------------------
if has_change '^src/runtime/'; then
  if   has_change '^docs/STATUS\.md$'; then okf "R1: STATUS.md aggiornato (src/runtime/**)"
  else fail "R1: src/runtime/** toccato ma docs/STATUS.md assente"
  fi
  if   has_change '^docs/NEXT_STEPS\.md$'; then okf "R1: NEXT_STEPS.md aggiornato"
  else fail "R1: src/runtime/** toccato ma docs/NEXT_STEPS.md assente"
  fi
fi

# -- R2: include/chronon3d/** ⇒ ADR esistente -----------------------------------------
if has_change '^include/chronon3d/'; then
  adr_count=$(ls -1 docs/adr/ADR-*.md 2>/dev/null | wc -l)
  if [[ "$adr_count" -ge 7 ]]; then
    okf "R2: ADR slot popolati (${adr_count})"
  else
    if apply_lvl fail; then
      note "R2: include/chronon3d/** toccato ma solo ${adr_count} ADR (atteso >= 7)"
    fi
  fi
fi

# -- R3: src/render_graph/** OR include/chronon3d/render_graph/ ⇒ ADR-001/-002 ----------
if has_change '^(src|include/chronon3d)/render_graph/'; then
  if   [[ -f docs/adr/ADR-001-frame-graph-compiler.md \
       && -f docs/adr/ADR-002-render-runtime-ownership.md ]]; then \
       okf "R3: ADR-001 + ADR-002 presenti"
  else
    note "R3: render_graph/** toccato ma ADR-001 o ADR-002 assenti"
  fi
fi

# -- R4: vcpkg.json OR CMakePresets.json ⇒ piano 08 ------------------------------------
if has_change '^(vcpkg\.json|CMakePresets\.json)$'; then
  if has_change '^docs/stabilization-plan/08-dependency-profiles\.md$'; then
    okf "R4: piano 08 aggiornato (vcpkg/CMakePresets)"
  else
    fail "R4: vcpkg.json o CMakePresets.json toccati ma 08-dependency-profiles.md assente"
  fi
fi

# -- R5: chiusura TICKET-NNN ⇒ CHANGELOG.md --------------------------------------------
commit_msg="$(git log -1 --pretty=%B HEAD 2>/dev/null || true)"
if printf '%s' "$commit_msg" | grep -Eq 'TICKET-[0-9]+|Closes? #[0-9]+|Resolves? #[0-9]+'; then
  if has_change '^docs/CHANGELOG\.md$'; then
    okf "R5: CHANGELOG.md aggiornato (commit chiude ticket)"
  else
    note "R5: commit chiude ticket ma docs/CHANGELOG.md non aggiornato"
  fi
else
  if has_change '^docs/CHANGELOG\.md$'; then
    okf "R5: CHANGELOG.md modificato (commit di chiusura)"
  fi
fi

# -- Esito finale ------------------------------------------------------------------------
echo
if [[ "$errs" -ne 0 ]]; then
  echo "FAILED: ${errs} violation(s), ${warn} warning(s)" >&2
  if [[ "$wip" -eq 1 ]]; then echo "(wip mode enabled — warning treated as ok)"; fi
  if [[ "$errs" -gt 0 && "$wip" -ne 1 ]]; then exit 1; fi
  if [[ "$wip" -eq 1 && "$errs" -eq 0 ]]; then exit 0; fi
  exit 1
fi
echo "OK: doc-sync invariants hold (range=${diff_range})"
exit 0

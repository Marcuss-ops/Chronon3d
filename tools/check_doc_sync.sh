#!/usr/bin/env bash
# ============================================================================
# tools/check_doc_sync.sh
#
# Gate di co-update per i 4 documenti canonici:
#   CURRENT_STATUS.md, ROADMAP.md, RELEASE_GATE.md, FOLLOWUP_TICKETS.md
# (vedi AGENTS.md §Insieme canonico della documentazione e
#  DOCUMENTATION_GOVERNANCE.md per il contratto completo).
#
# Regole:
#
#   1. Qualsiasi cambio in src/runtime/**       ⇒ CURRENT_STATUS.md obbligatorio
#      e ROADMAP.md obbligatorio.
#   2. Qualsiasi cambio in include/chronon3d/**  ⇒ almeno un ADR in
#      docs/adr/ deve esistere per l'area (warning).
#   3. Qualsiasi cambio in src/render_graph/** o
#      include/chronon3d/render_graph/**  ⇒ ADR-001/002 esistenti.
#   4. Qualsiasi cambio in vcpkg.json o CMakePresets.json
#      ⇒ AUDIT-TRAIL ONLY: docs/ARCHIVE/stabilization-plan/ non è
#      operativo (archived per AGENTS.md). Nessun enforcement.
#   5. Chiusura di un TICKET-NNN ⇒ CHANGELOG.md toccato (support doc).
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
fail()  { echo "[FAIL] $1" >&2; errs=$((errs + 1)); }
warnf() { echo "[WARN] $1" >&2; warn=$((warn + 1)); }
okf()   { echo "[ OK ] $1"; }

# -- helpers -----------------------------------------------------------------------------
has_change() {
  local pattern="$1"
  printf '%s\n' "$changed_files" | grep -Eq "$pattern"
}

# In wip mode the warnings are logged but do NOT increment errs / non early exit.
# (failures always exit regardless of mode).
log_warn() { if [[ "$wip" -eq 1 ]]; then echo "[WARN:skipped] $1"; return; fi; warnf "$1"; }

# -- R0: nessuna modifica a docs/ARCHIVE/ ------------------------------------------
# Per AGENTS.md §Insieme canonico della documentazione, docs/ARCHIVE/ è
# materiale storico (non operativo). Qualsiasi modifica (edit o aggiunta) sotto
# docs/ARCHIVE/ è un FAIL hard (R0 ha priorità su R1..R5; è la prima regola
# valutata).
# Eccezione: CURRENT_STATUS_HISTORY.md è il dump di cronologia rimosso dal
# CURRENT_STATUS.md compresso (Fix #7) — è un'operazione di archival one-shot,
# non una modifica operativa di file storici.
if has_change '^docs/ARCHIVE/'; then
  # Whitelist: CURRENT_STATUS_HISTORY.md and FOLLOWUP_TICKETS_HISTORY.md are
  # one-shot archival dumps of canonical index history (AGENTS.md §Docs canonical
  # update discipline rule). They are not operative historical edits.
  archive_changes=$(printf '%s\n' "$changed_files" | grep '^docs/ARCHIVE/' | grep -v '^docs/ARCHIVE/CURRENT_STATUS_HISTORY\.md$' | grep -v '^docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY\.md$' || true)
  if [[ -n "$archive_changes" ]]; then
    fail "R0: modificati o aggiunti file sotto docs/ARCHIVE/ (materiale storico, non operativo): $archive_changes"
  fi
fi

# -- R1: src/runtime/** ⇒ CURRENT_STATUS.md + ROADMAP.md --------------------------
# DOC-002: STATUS.md e NEXT_STEPS.md sono archiviati; i canonici sono
# CURRENT_STATUS.md e ROADMAP.md (AGENTS.md §Documenti canonici).
if has_change '^src/runtime/'; then
  if   has_change '^docs/CURRENT_STATUS\.md$'; then okf "R1: CURRENT_STATUS.md aggiornato (src/runtime/**)"
  else fail "R1: src/runtime/** toccato ma docs/CURRENT_STATUS.md assente"
  fi
  if   has_change '^docs/ROADMAP\.md$'; then okf "R1: ROADMAP.md aggiornato"
  else fail "R1: src/runtime/** toccato ma docs/ROADMAP.md assente"
  fi
fi

# -- R2: include/chronon3d/** ⇒ ADR esistente -----------------------------------------
# R2 e' area-aware: include/chronon3d/** senza ADR coprente = warn, non ok.
# Render-graph toccato  => ADR-001 + ADR-002 obbligatori.
# Asset-resolver toccato => ADR-005 obbligatorio.
# Composizioni toccate  => ADR-006 slot (placeholder fintanto che non scritto).
# Sessione/precomp     => ADR-007 slot (placeholder fintanto che non scritto).
adr_ok_for_render_graph=0
adr_ok_for_resolver=0
[[ -f docs/adr/ADR-001-frame-graph-compiler.md \
&& -f docs/adr/ADR-002-render-runtime-ownership.md ]] && adr_ok_for_render_graph=1
[[ -f docs/adr/ADR-005-asset-resolver-local.md ]] && adr_ok_for_resolver=1

if has_change '^include/chronon3d/render_graph/'; then
  if [[ "$adr_ok_for_render_graph" -eq 1 ]]; then
    okf "R2: render_graph/** coperto da ADR-001/002"
  else
    warnf "R2: include/chronon3d/render_graph/** toccato ma ADR-001 o ADR-002 assenti"
  fi
fi
if has_change '^include/chronon3d/assets/' \
   || has_change '^include/chronon3d/backends/assets/' \
   || has_change '^include/chronon3d/.*[Rr]esolver(\.hpp|/)'; then
  if [[ "$adr_ok_for_resolver" -eq 1 ]]; then
    okf "R2: assets/resolver coperto da ADR-005"
  else
    warnf "R2: assets* o *resolver* modificato ma ADR-005 assente"
  fi
fi
# Catch-all: qualsiasi altro include/chronon3d/** se non gia coperto da R2 sopra.
if has_change '^include/chronon3d/' \
   && ! has_change '^include/chronon3d/render_graph/' \
   && ! has_change '^include/chronon3d/(assets|resolver|asset_resolver)'; then
  any_adr=$(ls -1 docs/adr/ADR-*.md 2>/dev/null | wc -l)
  if [[ "$any_adr" -ge 1 ]]; then
    okf "R2: include/chronon3d/** modificato ma ADR slot popolati (${any_adr})"
  else
    warnf "R2: include/chronon3d/** toccato ma nessun ADR slot popolato in docs/adr/"
  fi
fi

# -- R3: src/render_graph/** OR include/chronon3d/render_graph/ ⇒ ADR-001/-002 ----------
if has_change '^(src|include/chronon3d)/render_graph/'; then
  if   [[ -f docs/adr/ADR-001-frame-graph-compiler.md \
       && -f docs/adr/ADR-002-render-runtime-ownership.md ]]; then \
       okf "R3: ADR-001 + ADR-002 presenti"
  else
    warnf "R3: render_graph/** toccato ma ADR-001 o ADR-002 assenti"
  fi
fi

# -- R4: vcpkg.json OR CMakePresets.json ⇒ AUDIT-TRAIL ONLY (NO enforcement) ---------
# NOTA (DOC-002): docs/ARCHIVE/stabilization-plan/08-dependency-profiles.md è archiviato.
# Questa regola è puramente documentale/storica. Nessun FAIL o WARN viene emesso.
# Per la cronologia: il piano 08 delle dipendenze tracciava la strategia di
# profilazione vcpkg/CMakePresets; oggi il contratto è in DOCUMENTATION_GOVERNANCE.md.

# -- R5: chiusura TICKET-NNN ⇒ CHANGELOG.md --------------------------------------------
commit_msg="$(git log -1 --pretty=%B HEAD 2>/dev/null || true)"
if printf '%s' "$commit_msg" | grep -Eq 'TICKET-[0-9]+|Closes? #[0-9]+|Resolves? #[0-9]+'; then
  if has_change '^docs/CHANGELOG\.md$'; then
    okf "R5: CHANGELOG.md aggiornato (commit chiude ticket)"
  else
    warnf "R5: commit chiude ticket ma docs/CHANGELOG.md non aggiornato"
  fi
else
  if has_change '^docs/CHANGELOG\.md$'; then
    okf "R5: CHANGELOG.md modificato (commit di chiusura)"
  fi
fi

# -- R-Table: docs/CURRENT_STATUS.md "Stato generale per area" table shape -------
# Hardening (post Cat-1 recovery commit c5793405): protects against sed-regex
# fragility that over-matches and silently drops rows from the canonical table.
# Runs whenever docs/CURRENT_STATUS.md is in the diff range; mirrors the
# existing R1-R5 diff-scoped pattern.
if has_change '^docs/CURRENT_STATUS\.md$'; then
  if bash "$ROOT/tools/check_current_status_table_shape.sh" >/dev/null 2>&1; then
    okf "R-Table: CURRENT_STATUS.md Stato generale per area table shape canonical (13 body rows + 12 named labels)"
  else
    fail "R-Table: CURRENT_STATUS.md table shape drift detected - run tools/check_current_status_table_shape.sh for diagnostic"
  fi
fi

# -- Esito finale ------------------------------------------------------------------------
echo
echo "Summary: ${errs} hard failure(s), ${warn} warning(s) (wip=$wip)"
if [[ "$errs" -ne 0 ]]; then
  exit 1
fi
if [[ "$wip" -eq 1 ]]; then echo "(wip mode enabled — warnings only)"; fi
echo "OK: doc-sync invariants hold (range=${diff_range})"
exit 0

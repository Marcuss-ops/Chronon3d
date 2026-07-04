# TICKET-M1.5#7-AUDIT — forward-decl + `std::unique_ptr<X>` incomplete-type pattern audit

> **Status:** PLANNED (proactive, no code changes at inception; preventive build correction).
>
> **Area:** Text backend / Build rot prevention (AGENTS.md v0.1 Cat-1 + Cat-3 freeze-compliant).
>
> **Vincoli architetturali:** nessuna nuova API pubblica, nessun nuovo singleton/registry/cache. Lo script di audit è un tool diagnostico (git-tracked, install-pipeline plumbing) che produce una lista esaustiva di siti dove il pattern è presente o è stato storicamente rotto.
>
> **Source of truth:** this file is the canonical spec; eventuali implementazioni successive dell'audit script (`tools/audit_incomplete_type_pattern.sh`) dovranno essere verificate contro questa spec.

## Background (causalità osservata)

GCC 15 (e clang in alcune versioni) emette `error: invalid application of 'sizeof' to incomplete type` quando un `std::unique_ptr<X>` viene istanziato con `X` forward-declared nell'header ma full-type NON incluso nel .cpp TU che possiede il field. Il default deleter richiede `sizeof(X)` per invocare `~X()` correttamente.

**Caso 1 (chiuso, commit `5320eb29`):** `src/backends/software/runtime_adapter.cpp` possiede `m_text_resources` (o campo equivalente) con `std::unique_ptr<chronon3d::TextRenderResources>` dichiarato in `include/chronon3d/backends/software/software_renderer.hpp` come forward-decl. Errore GCC 15: `error: invalid application of 'sizeof' to incomplete type 'chronon3d::TextRenderResources' at /usr/include/c++/15/bits/unique_ptr.h:90`. Fix: aggiunto `#include <chronon3d/backends/text/text_render_resources.hpp>` al .cpp TU.

**Caso 2 (chiuso, M1.5#8 carry-back incluso in commit `c9415e5b`):** `include/chronon3d/backends/software/software_session_resources.hpp` aveva `TextRenderResources* text_resources{nullptr}` come puntatore raw (OK, sizeof non serve), MA il M1.5#7 rewiring ha introdotto la dipendenza dal tipo completo in altri TUs (es. `runtime_adapter.cpp` accede `text_resources->field`), e il check sizeof-check del default move-ctor in-header ha riesposto lo stesso root cause. Fix: aggiunto full-include.

Entrambi i casi sono la stessa root cause (forward-decl + .cpp-full-include mancante), scoperti durante la verifica del TICKET-GATE-10-AR-RACE-MITIGATION (commit `140dc919`).

## Goal

Scansionare l'intero codebase `src/`, `include/chronon3d/`, `tests/` per il pattern:

1. `std::unique_ptr<TYPE>` (oppure `std::shared_ptr<TYPE>`) in un header `*.hpp` dove `TYPE` è solo forward-declared (`class TYPE;` o `struct TYPE;`) nello stesso header.
2. Verificare che il .cpp TU corrispondente (che effettivamente istanzia il default deleter) abbia il full-include di `TYPE`.
3. Identificare i siti dove il pattern è ROTTO (full-include mancante) — questi sono candidati per futuri commit di fix atomico (1 commit per caso rilevato).
4. Identificare i siti dove il pattern è CORRETTO (full-include presente) — questi sono la baseline di riferimento.

## Deliverable

Nuovo tool diagnostico `tools/audit_incomplete_type_pattern.sh` (git-tracked, install-pipeline plumbing) con il seguente pseudo-spec:

```bash
#!/usr/bin/env bash
# tools/audit_incomplete_type_pattern.sh
# Audit script: find forward-decl + std::unique_ptr/std::shared_ptr<incomplete_type>
# patterns that may fail to compile with GCC 15+.

set -euo pipefail

# Step 1: For each header in include/chronon3d/, extract every
# std::unique_ptr<TYPE> and std::shared_ptr<TYPE> token.
# For each such token, check if TYPE is forward-declared in the same header
# (search for `class TYPE;` or `struct TYPE;`).
# If forward-declared, list the header + token + line number.

# Step 2: For each candidate from Step 1, find the .cpp TU that "owns" the
# field (heuristic: same basename .cpp in src/, OR grep for the field name).
# Check if that .cpp TU has the full-include of TYPE.

# Step 3: Emit report to stdout:
#   - BROKEN: header field + missing full-include in .cpp TU (action required)
#   - OK: header field + full-include present in .cpp TU (baseline)
#   - UNCERTAIN: cannot determine ownership (manual review required)

# Exit code: 0 if all candidates are OK or UNCERTAIN; 1 if any BROKEN found.
```

Lo script deve essere:
- Idempotente (re-runnable con zero diff)
- Non invasivo (no modifications al codebase; solo report)
- In CI-friendly output format (parsable per future ticket-per-case)

## Step plan (follow-up sequence)

| Step | Scope | Commit | Stato |
|---|---|---|---|
| 1 | Scrivere `tools/audit_incomplete_type_pattern.sh` + AGENTS.md Cat-1/Cat-3 freeze note | questo commit | PLANNED |
| 2 | Eseguire lo script su `main` e produrre report iniziale | doc-only | PLANNED |
| 3 | Per ogni `BROKEN` site, aprire 1 ticket dedicato + 1 atomic commit di fix | singolo commit per ticket | PLANNED |
| 4 | Per ogni `UNCERTAIN` site, review manuale + classificazione OK / BROKEN | doc-only | PLANNED |
| 5 | Re-run audit script dopo tutti i fix; atteso exit 0 + report 100% OK | doc-only | PLANNED |

## AGENTS.md v0.1 vincoli

- Cat-1 freeze: build correction, install-pipeline plumbing. ✓ (script è plumbing diagnostico).
- Cat-3 freeze: regression-gate verification + legacy cleanup. ✓ (audit previene future rot).
- Cat-2/4/5 non applicabili: nessun test deterministico aggiunto in questo commit (l'audit script è doc-only all'inizio), nessun consumer SDK esterno modificato, nessun cambio canonical doc oltre questo file.

## Statistiche attese post-completion

- `tools/audit_incomplete_type_pattern.sh`: ~50-80 LOC (bash + awk + grep).
- Tickets aperti per `BROKEN` sites: 1-N (dipende dallo scan iniziale; stimato 0-3 dato che i 2 casi noti sono già chiusi).
- Atomic-commit per ogni BROKEN site: 1 file modificato per commit (il .cpp TU con full-include aggiunto).
- Public API surface delta: 0 (nessuna API pubblica toccata).
- Build-system impact: 0 (no CMakeLists references aggiunte; script è standalone).

## Cross-references

- [`docs/FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md) §P1 backlog (TICKET-M1.5#7-AUDIT row, questo commit).
- [`docs/CHANGELOG.md`](../CHANGELOG.md) §Luglio 2026 (entry TICKET-RUNTIME-ADAPTER-INCOMPLETE-TYPE che ha generato questo audit follow-up).
- [`docs/CURRENT_STATUS.md`](../CURRENT_STATUS.md) §"Stato generale per area" Text Production V1 row (aggiornare in ogni Step 3 commit).
- [`AGENTS.md`](../../AGENTS.md) §Cosa è CONSENTITO durante feature freeze (Cat-1 build correction + Cat-3 legacy removal).
- TICKET-RUNTIME-ADAPTER-INCOMPLETE-TYPE (lineage, chiuso post dual-include-fix).
- TICKET-M1.5#7-FULL-SPLIT (parallel M1.5#7 follow-up, structural split del monolitico `text_render_resources.hpp`).
- TICKET-M1.5#8-PRE-EXISTING-ROT (parallel M1.5#8 follow-up, pre-existing LINK rot in `chronon3d_core_tests`).

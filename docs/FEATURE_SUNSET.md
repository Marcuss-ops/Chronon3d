# Registro Sunset Feature (Test 16)

**Regola dei "Tre Non"**: Una feature è candidata al sunset (`MOVE` a `content/experimental/` OR `git rm`) se soddisfa TUTTI e tre:

1. **NON usata-in-produzione**: nessun caller attivo in `src/`, `apps/`, o runtime registries di `content/register_content_modules.cpp`.
2. **NON misurata in benchmark**: assente da `tools/perf/` benchmarks o `docs/baselines/` snapshots entro gli ultimi 30 giorni.
3. **NON necessaria-al-cliente**: nessuna referenza in `tools/install_consumer_test.sh` boundary contracts, `examples/`, o `content/experimental/` consumer integration.

**Scadenza 30gg**: 30 giorni dalla entry nel registro → escalation automatica da `DEFERRED` → `MOVE/DELETE` quando la riga soddisfa TUTTI e tre i "non".

**Cert discipline §honesty** (AGENTS.md v0.1): tutte le entry `DEFERRED` su env-blocked VPS hanno `verified="PARTIAL"` + `timing_basis="static cross-ref on env-blocked VPS"`.

**5 candidati DEFERRED al 2026-07-12 (env-blocked VPS per §honesty)**:

| # | Feature | Scadenza | Usata-in-produzione | Benchmark-o-cliente | Verdict |
|---|---|---|---|---|---|
| 1 | `content/common/` (3 .hpp) | 2026-08-11 | **SÌ** in 3 test files via `#include "content/common/..."` | non misurata | **DEFERRED-VERIFY** |
| 2 | `content/ae_parity/` (4 file .hpp/.cpp) | 2026-08-11 | PARZIALE in `content/CMakeLists.txt`, 0 `#include` diretti in `src/` | usata da `ae_parity_referee/` | **DEFERRED-VERIFY** |
| 3 | `content/text_placement/` (2 file) | 2026-08-11 | **SÌ** in Text V1 pipeline | usata in `text_definition_tests.cmake` | **DEFERRED-VERIFY** |
| 4 | `content/backgrounds/` (2 file) | 2026-08-11 | PARZIALE — showcase only | non misurata | **DEFERRED-VERIFY** |
| 5 | `content/examples/` (educational obsoleto) | 2026-08-11 | **NO** — nessuna referenza runtime | non misurata | **DEFERRED-VERIFY** |

**Forward-point operative (next cycle)**:
1. `TICKET-SUNSET-VERIFY` workspace-level con `cmake --build` + golden tests run
2. `tools/check_feature_sunset.sh` gate (Cat-4 ancillary deferred)
3. AGENTS.md §Doc governance codification: regola "newer-at-top" per registry

**Riferimenti interni**:
- `docs/FEATURES.md` — feature inventory canonica
- `content/experimental/` — destinazione MOVE post-validazione
- `tools/install_consumer_test.sh` — boundary contract
- `AGENTS.md v0.1 §honesty` — disciplina PARTIAL cert
- `backup-sunset-test-16` annotated tag — audit trail anchor (pre-cycle-3 finale)

# Baseline verde

> **Fonte canonica del dettaglio:** [`docs/01-baseline-green.md`](../01-baseline-green.md) — definizione operativa del baseline verde verificabile, test verdi/rossi, comandi di riproduzione. Vedi anche [`docs/02-determinism.md`](../02-determinism.md) per il contratto determinismo.

## Stato sintetico

| Gate | Stato |
|---|---|
| Build `linux-core-dev` | 🟢 Verde |
| Build `linux-lean` | 🟢 Verde |
| Build `linux-lean-dev` | 🔴 Non verde |
| Test veloci | 🔴 704/707 (3 failure) |
| Gate architetturali | 🔴 2 violazioni |
| Consumer SDK esterno | 🔴 Non verde |

## Blocchi da chiudere in ordine

### 1. Tre test falliti
- [ ] `test_render_session_reset_and_isolation` (percorso concorrente).
- [ ] Test scene che chiama `RenderRuntime::backend()` prima di `attach_backend()`.
- [ ] Secondo test scene con stesso contratto backend non inizializzato.

### 2. Due violazioni architetturali
- [ ] Identificare e correggere le violazioni in `render_session.hpp`.
- [ ] Eseguire `chronon3d_architecture_includes_boundary` con exit code zero.

### 3. Preset `linux-lean-dev`
- [ ] Riprodurre il primo errore da build pulita.
- [ ] Correggere un solo lineage per commit.

### 4. Consumer SDK
- [ ] Correggere propagazione `CMAKE_TOOLCHAIN_FILE` / `CMAKE_PREFIX_PATH`.
- [ ] Installare, configurare ed eseguire consumer esterno.

## Working group: Fase 0 closure (WG-FASE0)

> **Sponsor:** Questo WG è sponsorizzato per portare il baseline della Fase 0 allo stato "Verde Verificabile", chiusura propedeutica ai DoD primo-milestone #6 (golden test determinismo), #9 (Consumer SDK), #2 (8+ preset subtitle).
>
> **Autorità di sign-off:** Arch-Lead o Core Maintainer designato può fare override sui singoli PR per sbloccare i gate, a patto che rispettino le policy [`AGENTS.md`](../../AGENTS.md) (no duplicazione registry/resolver/cache, no abusi di `DOCTEST_CONFIG_SUPER_FAST_ASSERTS`).

### Charter

Questo WG chiude 5 issue canoniche pre-esistenti:

- **3 test failure** (test runtime + 2 test scene con `backend()` non attached).
- **2 violazioni architetturali** (header inclusion patterns in `include/chronon3d/runtime/render_session.hpp` catturati da `tools/check_architecture_boundaries.sh`).

Senza chiusura di tutte e 5, i seguenti gate restano rossi:
- `tools/install_consumer_test.sh` (gates `linux-ci` Cluster C).
- `tools/check_architecture_boundaries.sh` (gate `gates.yml` Gate 5).

Cross-link diretti: [`docs/FOLLOWUP_TICKETS.md`](../../FOLLOWUP_TICKETS.md) per i 5 ticket associati, [`docs/DEFINITION_OF_DONE.md`](../DEFINITION_OF_DONE.md) per il per-PR DoD canon.

### Decomposizione in 5 sub-task

| # | Sub-task | Ticket | Owner role | Effort | Blocking risk |
|---|---|---|---|---|---|
| 1 | Arch violation: forbidden include pattern in `include/chronon3d/runtime/render_session.hpp` | TICKET-012 | Core maintainer | Small (<1d) | **High** — blocks `tools/check_architecture_boundaries.sh` (Gate 5) |
| 2 | Arch violation: sanction bypass in `render_session.hpp` | TICKET-013 | Core maintainer | Small (<1d) | **High** — blocks Gate 5 |
| 3 | Test fail: `tests/runtime/test_render_session_reset_and_isolation.cpp` (concurrent path rot) | TICKET-014 | Runtime maintainer | Medium (1–2d) | **Medium** — blocks DoD #6 |
| 4 | Test fail (Scene 1): `RenderRuntime::backend()` called before `attach_backend()` | TICKET-015 | Scene-graph owner | Small (<1d) | **Low** — isolated scope |
| 5 | Test fail (Scene 2): same contract as #4 in second scene file | TICKET-016 | Scene-graph owner | Small (<1d) | **Low** — isolated scope |

### Sequencing (ETTR critical path ~2 giorni)

```text
[ Critical Path (2 sub-task in serie) ]

TICKET-012 ─┐
            ├──> [ Gate 5 🟢 verde ] ──> [ install_consumer_test.sh 🟢 verde ]
TICKET-013 ─┘                                  │
                                               ▼
                                  [ DoD Cluster C sbloccato ]

[ Test stabilization in parallelo ]

TICKET-014 (concurrent path) ─────────────────────> |
TICKET-015 (scene 1, backend()) ──────────────────> |
TICKET-016 (scene 2, backend()) ──────────────────> | ──> [ WG-FASE0 🟢 Done ]
```

Le 2 arch violations vanno in serie perché entrambe cadono sotto Gate 5; i 3 test fail sono parallelizzabili (3 righe/team indipendenti).

### Acceptance criteria (machine-verificabile, per [`docs/DEFINITION_OF_DONE.md`](../DEFINITION_OF_DONE.md))

- `tools/check_architecture_boundaries.sh` esce con **`RC=0`** in tutte le combinazioni `linux-{core-dev,lean-dev,ci-nocontent,ci,debug,full-validation,asan}`.
- `tools/install_consumer_test.sh` esce con **`RC=0`** sotto preset `linux-ci`, salvando il target `Chronon3D::SDK` correttamente.
- 0 fallimenti in `tests/runtime/test_render_session_reset_and_isolation.cpp` e nei due test scene con `backend()`.
- `cmake --preset linux-debug && cmake --build build/chronon/linux-debug --target chronon3d_tests_fast` ritorna **`RC=0`** con 707/707 test verde (cross-ref `docs/01-baseline-green.md` §1).
- Nessuna regressione in DoD §6 (golden determinismo, post-acquisizione hash numerico su `linux-debug`).

### Cadence operativa

- **Sync bisettimanale** (lunedì e giovedì) sullo stato dei 5 ticket.
- **Single-overhead commit policy**: ogni sub-task deve essere chiudibile in **≤ 1 PR verde**. Se una fix richiede > 1 PR, forzare "≤ 1 linea verde" per [`AGENTS.md`](../../AGENTS.md) (es. lock mirato o ripristino per-session state isolato).
- **No skipping**: vietato usare `doctest::skip()` o mascherare errori per sbloccare la Fase 0. Violazione esplicita di [`AGENTS.md`](../../AGENTS.md).

### Rischi pre-mortem (5 item)

1. **PR Size Creep** — la fix del path concorrente (`TICKET-014`) può espandersi in refactor. *Mitigazione*: split atomic, lock minimo.
2. **False Green Skipping** — tentazione di `doctest::skip()` o silencing. *Mitigazione*: enforcement per AGENTS.md + code-reviewer obbligatorio per ogni PR.
3. **Consumer SDK Environment Drift** — `install_consumer_test.sh` può mascherare errori vcpkg come test failure. *Mitigazione*: forza `--preset linux-ci-nocontent` prima di marcare "verde".
4. **Header Cascades** — togliere un include in `render_session.hpp` può rompere decine di file. *Mitigazione*: forward-declaration proattiva prima del delete.
5. **Definizioni divergenti di "Verde"** — interpretazioni non-uniformi. *Mitigazione*: solo exit-code `RC=0` conta; niente "sembra verde".

### Cross-reference ad altri doc da aggiornare downstream

- [`docs/01-baseline-green.md`](../../01-baseline-green.md) §1: aggiungere back-link alla sezione WG in questo file.
- [`docs/stabilization-plan/README.md`](README.md): aggiungere "WG-FASE0" all'indice dei work package.
- [`docs/STATUS.md`](../../STATUS.md): marcare "WG-FASE0" a 🔴 In progress.
- [`docs/NEXT_STEPS.md`](../../NEXT_STEPS.md): assegnare slot operativo a TICKET-012+TICKET-013 del WG-FASE0 (critical path).
- [`docs/FOLLOWUP_TICKETS.md`](../../FOLLOWUP_TICKETS.md): TICKET-012/013/014/015/016 inizializzati con schema standard (v. sezione sotto).

## Completato quando

- Tutti i target obbligatori restituiscono exit code zero.
- Nessun test fallisce e nessun gate architetturale fallisce.
- Il consumer installato configura, compila ed esegue.
- **WG-FASE0 chiuso**: i 5 ticket canonici (TICKET-012/013/014/015/016) sono tutti `🟢 Done` con criterio di acceptance superato in CI.

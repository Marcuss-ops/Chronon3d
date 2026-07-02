# Chronon3D — Current Status

> **Snapshot:** `main@7c55c735` (commit `7c55c735`, `fix(graph): Result<OwnedFB, NodeExecutionError> — TextRunNode backend errors propagate to frame failure (P0 #1)`) — 2026-07-02. Linux-only.
>
> **Ultima baseline macchina-verificata:** `main@aaf70032` (10/11 PASS — vedi [`docs/baselines/main-aaf70032-baseline.md`](docs/baselines/main-aaf70032-baseline.md)).
> **Baseline corrente:** `main@7c55c735` — **9/11 PASS** (NON VERDE).
> Gate #4 FAIL: `check_gitignored_dirs.sh` — `reports/perf/main-73a2aa9b-gates.json` leaked as tracked file (absolute-path leak).
> Gate #10 FAIL: `install_consumer_test.sh` — build rot pre-esistente (`render_session.cpp` namespace mismatch + `software_backend.cpp` incomplete type `RenderNode`).
> Gate #8 borderline PASS: `check_filename_drift.sh` exit 0 ma 66 warning drift.
>
> Tra `aaf70032` e l'HEAD corrente: TICKET-118 + TICKET-119 closures, P0 #1 (`Result<OwnedFB, NodeExecutionError>` su `RenderGraphNode::execute()`), P0 #2 (`FontLayoutIdentity` unificata su 6 location), P1 #1–#5 fixes.
>
> Documenti canonici (vedi [`docs/DOCUMENTATION_GOVERNANCE.md`](docs/DOCUMENTATION_GOVERNANCE.md) per il contratto):
> - Regole operative / feature freeze: [`AGENTS.md`](../AGENTS.md)
> - Roadmap / futuro: [`docs/ROADMAP.md`](docs/ROADMAP.md)
> - Requisiti permanenti di release: [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md)
> - Indice blocker aperti: [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md)
> - Chiusure recenti: sezione `Recently closed` in FOLLOWUP_TICKETS + [`docs/CHANGELOG.md`](docs/CHANGELOG.md)

## Come leggere gli stati

| Stato     | Significato                                                                  |
| --------- | ---------------------------------------------------------------------------- |
| PASS      | Implementazione verificata contro prova eseguibile osservata sul commit indicato. |
| FAIL      | Comportamento non corretto osservato.                                         |
| PARTIAL   | Implementazione presente ma con limiti o copertura incompleta.               |
| NOT RUN   | Gate / prova non ancora eseguita in macchina-verifica sul commit corrente.    |
| BLOCKED   | Bloccato da un altro ticket o da una condizione esterna.                     |
| PLANNED   | Design presente, implementazione non iniziata o non completata.               |

Un valore `PASS` deve indicare lo SHA e la baseline che lo dimostrano — altrimenti è un falso positivo.

## Stato generale per area

| Area                                            | Stato    | Note sintetiche                                                          |
| ----------------------------------------------- | -------- | ------------------------------------------------------------------------ |
| Render graph compilato                          | NOT RUN  | Baseline completa da verificare sul commit candidato.                    |
| Software backend                                | PASS     | Gate-3 (I1-I5) tutto verde su `main@775da4d9`. TICKET-077 + TICKET-079 chiusi. |
| Execution scope (precomp + nested)              | NOT RUN  | Lease, child arena e concorrenza da chiudere.                            |
| Text Production V1                              | NOT RUN  | word timing, rich text produttivo, preset, golden da chiudere.           |
| Camera Production V1                            | NOT RUN  | link sbloccato (TICKET-029); migrazione legacy aperta.                  |
| SDK C++ installabile                            | NOT RUN  | consumer di rendering reale con testo + camera → PNG in certificazione.   |
| SDK cross-language                              | NOT RUN  | C ABI e formato `.chronon` da progettare.                                |
| Sistemi meta (Expressions V2 / V3 tile-first)   | PLANNED  | Expressions V2 OFF di default, non installato. V3 subordinato a V1.      |
| Render runtime (session + caches)               | PARTIAL  | P1 #3: `RenderSession::layout_cache` aggiunto (by-value), `shared_text_layout_cache()` deprecato, callsite migration post-baseline. |

## Blocker correnti per baseline verde (top 10 attivi)

Per le specifiche di accettazione complete di ogni ticket vedi [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) (canonical).
Per la storia delle chiusure vedi `Recently closed` in `FOLLOWUP_TICKETS.md` + [`docs/CHANGELOG.md`](docs/CHANGELOG.md).

| ID          | Area                                                                  | Stato    | Blocca                              |
| ----------- | --------------------------------------------------------------------- | -------- | ----------------------------------- |

| TICKET-036  | chronon3d_camera_architecture_gate P0                                 | PLANNED  | arch-boundary gate 5/6              |
| TICKET-044  | arch_boundaries_selftest hardcoded paths                              | PLANNED  | arch-boundary gate 5                |
| TICKET-046  | filename drift stale references                                       | PLANNED  | arch-boundary gate 5                |
| TICKET-011  | pre-existing mainline build rot                                       | PLANNED  | arch-boundary gate 1–8              |
| TICKET-005  | post-cascade cleanup                                                  | PARTIAL  | arch-completeness gate 5            |
| TICKET-118  | `SoftwareBackend::draw_node` real impl + dummy `TextRunProcessor` drop | Done     | cat-3 fake-success closure           |
| TICKET-119  | `SoftwareBackend` m_owner back-pointer removal + internal bridge       | Done     | cat-3 arch-debt closure              |
| P0-1        | `RenderGraphNode::execute()` → `Result<OwnedFB, NodeExecutionError>`      | Done     | P0 false-success pattern chiuso      |
| P0-2        | `FontLayoutIdentity` unificata su cache/hash/fastpath/prewarm              | Done     | P0 font identity fragmentation chiuso |
| TICKET-022  | camera double look-at compiled path                                   | PARTIAL  | arch-boundary gate 5/6              |
| TICKET-064  | §9 ExecutionScope — ScopeError/ScopeErrorCode structured error model | PARTIAL  | arch-boundary gate 5                |
| TICKET-051  | A4.3 per-preset visual diagnostic                                     | PLANNED  | A4.3 visual gate                    |

> Ticket chiusi di recente: vedi `Recently closed` in `FOLLOWUP_TICKETS.md` + [`docs/CHANGELOG.md`](docs/CHANGELOG.md).

## Certificazione corrente

Ultima baseline macchina-verificata: `main@aaf70032` — **10/11 PASS**.
Baseline corrente: `main@7c55c735` — **9/11 PASS** (NON VERDE; gate #4 FAIL — `reports/perf/main-73a2aa9b-gates.json` tracked file leak; gate #10 FAIL — build rot pre-esistente).
Per la revoca del **feature freeze** (vedi `AGENTS.md`) è richiesto **11/11 PASS sullo stesso commit**.
Log della run: [`reports/perf/main-73a2aa9b-gates.json`](../reports/perf/main-73a2aa9b-gates.json) (11/11 gate eseguiti sequenzialmente; short SHA `73a2aa9b` / full SHA `73a2aa9b31803e844ff4e69110735ce4d74f02f3`).
Per la revoca del **feature freeze** (vedi `AGENTS.md`) è richiesto **11/11 PASS sullo stesso commit**.
Storico baseline: [`docs/baselines/`](docs/baselines/) (file immutabili per SHA, una sola baseline per commit).

## Prossimo passo operativo

Ottenere un commit `main@<X>` con 11/11 PASS macchina-verificati per promuovere la baseline a CERTIFICATA, poi revocare formalmente il freeze. Nel frattempo i lavori consentiti sono solo nelle 5 categorie del freeze (build-fix, test deterministici, rimozione percorsi legacy, consumer SDK, allineamento documentazione — quest'ultima è la categoria del presente commit).

## Link canonici

- [`docs/ROADMAP.md`](docs/ROADMAP.md) — milestone prodotto.
- [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md) — requisiti permanenti di release.
- [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) — indice blocker aperti (canonical).
- [`docs/CHANGELOG.md`](docs/CHANGELOG.md) — chiusure recenti.
- [`docs/baselines/main-aaf70032-baseline.md`](docs/baselines/main-aaf70032-baseline.md) — ultima baseline macchina-verificata (10/11 PASS).
- [`docs/baselines/main-21103265-baseline.md`](docs/baselines/main-21103265-baseline.md) — baseline precedente (9/11 PASS).
- [`reports/perf/main-73a2aa9b-gates.json`](../reports/perf/main-73a2aa9b-gates.json) — log macchina-verificato della 11-gate run su `main@73a2aa9b` (10/11 PASS, gate #10 `install_consumer_test.sh` FAIL).
- [`docs/DOCUMENTATION_GOVERNANCE.md`](docs/DOCUMENTATION_GOVERNANCE.md) — contratto documentale (single-source-of-truth).
- [`docs/ARCHIVE/`](docs/ARCHIVE/) — materiale storico (non operativo; nessun riferimento operativo consentito).

_Limite raccomandato: 150 righe (vedi `DOCUMENTATION_GOVERNANCE.md` DoD §10)._

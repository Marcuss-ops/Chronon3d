# Chronon3D — Current Status

> **Snapshot:** `main@c40ba16f` (commit `c40ba16f3`, docs-sync atop Phase 4 fixes) — 2026-07-02. Linux-only.
>
> **Ultima baseline macchina-verificata:** `main@aaf70032` (10/11 PASS — vedi [`docs/baselines/main-aaf70032-baseline.md`](docs/baselines/main-aaf70032-baseline.md)).
> **Baseline corrente:** `main@c40ba16f` — **10/11 PASS** osservato (gate #10 fix pushed in due commit consecutivi: `21b9fb5d` CMake + `75035f2b` (ora `c40ba16f` post-rebase) runtime default-arg chain on `RenderPipeline::debug_graph`); Phase 4 end-to-end machine-verify ancora da confermare per promuovere a baseline.
> Gate #4 RISOLTO: `check_gitignored_dirs.sh` — `reports/perf/` aggiunto a `.gitignore` (commit `f6f700b1`).
> Gate #10 doppio fix pushed su `main`: (a) commit `21b9fb5d` cmake/Chronon3DRegistry.cmake case-fix TBB/xxHash + 44-entry bulk-insert cmake/Chronon3DPublicHeaders.cmake; (b) commit `75035f2b` runtime::RenderPipeline::debug_graph default-arg chain (`float fps = 0.0f` sentinel) per chiudere il C++ syntax error esposto da upstream `6df9b429`. Entrambi sono Cat-1 (build corrections).
> Gate #8 borderline PASS: `check_filename_drift.sh` exit 0 ma 155 warning drift.
>
> Tra `aaf70032` e l'HEAD corrente sono atterrati: TICKET-118/119, P0 #1–#2, P1 #1–#5 fixes, P1 #7 (`16efb496`), P1 #10 (`6df9b429` + `560750e3`), P1 #12 (`59b2439f`), gate #4 fix (`f6f700b1`), gate #10 analyze_scene_graph fix (`560750e3`), ticket P1-07..P1-12 individuali (`0295203d`), doc sync commits (`6d951079`, `96e6e88e`), CMake TitleCase + transitive header fix (`21b9fb5d`), runtime::RenderPipeline default-arg chain fix (`75035f2b` → `c40ba16f` post-rebase).
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
Baseline corrente: `main@c40ba16f` — **10/11 PASS osservato** (NON VERDE formale; gate #10 ha due fix pushed in `21b9fb5d` + `75035f2b`, ma manca la CI machine-verified Phase 4 verde per poter revocare il feature freeze).
Gate #4 (gitignored dirs) risolto da `f6f700b1`.
Gate #7 (doc-sync) PASS nella run corrente.
Gate #10: due patch pushed in `21b9fb5d` (cmake) + `75035f2b` (runtime). Per la revoca formale serve run `tools/install_consumer_test.sh` end-to-end su `c40ba16f` con tutte le fasi 1.1–4 verdi.
Per la revoca del **feature freeze** (vedi `AGENTS.md`) è richiesto **11/11 PASS sullo stesso commit**.
Storico baseline: [`docs/baselines/`](docs/baselines/) (file immutabili per SHA, una sola baseline per commit).

## Chiusure recenti (P1)

| P1 | Area | Commit | Stato |
|----|------|--------|-------|
| P1 #10 | Frame rate hardcoded | `6df9b429` + `560750e3` | ✅ DONE |
| P1 #12 | CMake ar merge + include_private | `59b2439f` | ✅ DONE |
| P1 #7 | Asset resolver globale | — | PLANNED |
| P1 #8 | Text renderer monolite | — | PLANNED |
| P1 #9 | RenderRuntime service locator | — | PLANNED |
| P1 #11 | Timeline percorsi multipli | — | PLANNED |

## Prossimo passo operativo

Chiudere gate #10 (install_consumer_test.sh) per raggiungere 11/11 PASS, poi revocare formalmente il feature freeze. I P1 #7, #8, #9, #11 sono pianificati post-baseline verde.

## Link canonici

- [`docs/ROADMAP.md`](docs/ROADMAP.md) — milestone prodotto (vedi TICKET-GATE-7-R1: Coverage src/runtime/** da aggiungere).
- [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md) — requisiti permanenti di release.
- [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) — indice blocker aperti (canonical).
- [`docs/CHANGELOG.md`](docs/CHANGELOG.md) — chiusure recenti.
- [`docs/baselines/main-aaf70032-baseline.md`](docs/baselines/main-aaf70032-baseline.md) — ultima baseline macchina-verificata (10/11 PASS).
- [`docs/baselines/main-21103265-baseline.md`](docs/baselines/main-21103265-baseline.md) — baseline precedente (9/11 PASS).
- [`reports/perf/main-73a2aa9b-gates.json`](../reports/perf/main-73a2aa9b-gates.json) — log macchina-verificato della 11-gate run su `main@73a2aa9b` (10/11 PASS, gate #10 `install_consumer_test.sh` FAIL).
- [`docs/DOCUMENTATION_GOVERNANCE.md`](docs/DOCUMENTATION_GOVERNANCE.md) — contratto documentale (single-source-of-truth).
- [`docs/ARCHIVE/`](docs/ARCHIVE/) — materiale storico (non operativo; nessun riferimento operativo consentito).

## Gate audit snapshot — `main@c40ba16f` (2026-07-02, post-rebase + runtime-fix)

| # | Gate                                        | Esito      | Note                                                                       |
|---|---------------------------------------------|------------|----------------------------------------------------------------------------|
| 1 | `check_architecture_boundaries.sh`          | ✅ PASS    | 14/15 check, 1 advisory (TICKET-042, non blocker).                        |
| 2 | `check_architecture_boundaries_selftest.sh` | ✅ PASS    | 15 assertions.                                                             |
| 3 | `check_software_renderer_boundary.sh`       | ✅ PASS    | I1-I5 tutti rispettati.                                                    |
| 4 | `check_gitignored_dirs.sh`                  | ✅ PASS    | Fissato da `f6f700b1` (`reports/perf/` in `.gitignore`).                 |
| 5 | `audit_software_renderer.sh`                | ✅ PASS    |                                                                            |
| 6 | `check_camera_architecture.sh`              | ✅ PASS    |                                                                            |
| 7 | `check_doc_sync.sh`                         | ✅ PASS    |                                                                            |
| 8 | `check_filename_drift.sh`                   | ⚠️ PASS*   | warn-mode; 155 drift warning.                                              |
| 9 | `test_architectural.sh`                     | ✅ PASS    | Static architecture-level rot: 0.                                          |
| 10 | `install_consumer_test.sh`                | ⚠️ FIX PENDING (double) | (a) `21b9fb5d` cmake case-fix + 44 transitive headers; (b) `75035f2b` runtime default-arg chain su `RenderPipeline::debug_graph`. End-to-end Phase 4 verde ancora da confermare. |
| 11 | `check_backend_sanitization.py`            | ✅ PASS    |                                                                            |

**Totale: 10/11 PASS** — gate #10 doppio-fix landed; machine-verified Phase 4 verde ancora richiesto per la revoca formale del feature freeze e per promuovere `c40ba16f` a baseline macchina-verificata.

_Limite raccomandato: 150 righe (vedi `DOCUMENTATION_GOVERNANCE.md` DoD §10)._

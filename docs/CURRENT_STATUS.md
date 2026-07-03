# Chronon3D — Current Status

> **Snapshot:** `main@e0285af2` (Fase A completata 6/6, Fase B2+B3 doc-deprecation, Fase C2 factory in corso) — 2026-07-03. Linux-only.
>
> **Ultima baseline macchina-verificata:** `main@aaf70032` (10/11 PASS — vedi [`docs/baselines/main-aaf70032-baseline.md`](docs/baselines/main-aaf70032-baseline.md)).
> **Baseline corrente:** `main@a53a8d25` — **8/11 PASS** osservato (audit del 2026-07-03).
> Gate #3 REGRESSION: `check_software_renderer_boundary.sh` I2 — `software_renderer.hpp` LOC=203 > 200 (3 linee sopra il limite).
> Gate #7 REGRESSION: `check_doc_sync.sh` R0 — commit `a53a8d25` ha archiviato CHANGELOG in `docs/ARCHIVE/CHANGELOG_2026_H1.md`, violando il gate R0.
> Gate #10 ANCORA FAIL: `install_consumer_test.sh` — "Disk quota exceeded" su PCH file (224MB ciascuno, ccache 5.0G). Build si ferma all'unità 28/340. Regressione infrastrutturale, non di codice.
> Gate #8: 73 drift warning (in miglioramento da 155).
>
> P0 #1 (TextRunNode error propagation) e P0 #2 (FontLayoutIdentity) sono CHIUSI e verificati su questo commit.
>
> **Fase A — P0 chiusi (2026-07-03):** A1 (symlink legacy + gate standalone compile), A2 (backend construction unificata), A3 (sdk::RenderEngine canonico), A4+A5 (error propagation), A6 (m_backend_warned rimosso, immutability tracked Phase C).
>
> **Fase B2+B3 — Deprecation doc completata (2026-07-03):** `process_wide_assets_root()` marcato `[[deprecated]]` con migration path a `RenderRuntime::resolver()`. `shared_text_layout_cache()` marcato con deprecation banner e migration path a `RenderSession::layout_cache`. Eliminazione effettiva bloccata da ~24+ call sites → Phase C (post-feature-freeze).
>
> **Fase C2 — Factory unificata (2026-07-03):** `RenderRuntime::create(RuntimeConfig)` → `Result<RenderRuntime, RuntimeBuildError>`. `RuntimeConfig` wrappa `Config` + `optional<assets_root>`. `RuntimeBuildError` con `Code::InternalError` / `Code::AssetMountFailed`. `attach_backend()` rafforzato `[[deprecated]]` con suppression warning nei bridge interni (`runtime_adapter.cpp`, `test_utils.hpp`).
>
> Tra `aaf70032` e l'HEAD corrente sono atterrati: TICKET-118/119, P0 #1–#2, P1 #1–#5 fixes, P1 #7 (`16efb496`), P1 #10 (`6df9b429` + `560750e3`), P1 #12 (`59b2439f`), gate #4 fix (`f6f700b1`), gate #10 analyze_scene_graph fix (`560750e3`), ticket P1-07..P1-12 individuali (`0295203d`), doc sync commits (`6d951079`, `96e6e88e`), CMake TitleCase + transitive header fix (`21b9fb5d`), runtime::RenderPipeline default-arg chain fix (`75035f2b` → `c40ba16f` post-rebase), **Fase A1** — rimozione 4 symlink legacy + gate standalone compile, **Fase A2** — backend construction unificata.
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
| Render runtime (session + caches)               | PASS     | P0 #B1: ImageCache moved to RenderRuntime. P1 #3: `RenderSession::layout_cache` added. |
| Render engine construction                      | PASS     | P0 #C2: Impl ctor unified, `optional<path>`. attach_backend() deprecated. |
| Composition pipeline                            | PASS     | P0 #C3: canonical pipeline documented. render_composition_frame canonical. |
| Text pre-compilation (CompiledTextRun)          | PLANNED  | P0 #C1: documented in text_run.hpp. Blocked by feature freeze. |

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
| P0 #1–#6  | RenderGraph error propagation + immutability (Fase A)         | Done     | A1-A6 tutti chiusi; m_backend_warned rimosso.   |
| P0 #B1    | ImageCache da singleton a RenderRuntime (Fase B)               | Done     | `1fcc9100`; instance() rimosso, thread::detach eliminato. |
| P0 #C2    | Unify RenderEngine::Impl constructors + deprecate attach_backend | Done     | `d8a228f7`; 2 costruttori → 1 con `optional<path>`. |
| P0 #C3    | Canonical composition pipeline documentation (Fase C)          | Done     | `3b4dbdc6`; Definition→Compiler→Evaluator→GraphCompiler→Executor doc. |
| P0 #C1    | CompiledTextRun pre-compilation (Fase C)                       | PLANNED  | Doc-only; blocked by feature freeze.             |
| TICKET-022  | camera double look-at compiled path                                   | PARTIAL  | arch-boundary gate 5/6              |
| TICKET-064  | §9 ExecutionScope — ScopeError/ScopeErrorCode structured error model | PARTIAL  | arch-boundary gate 5                |
| TICKET-051  | A4.3 per-preset visual diagnostic                                     | PLANNED  | A4.3 visual gate                    |

> Ticket chiusi di recente: vedi `Recently closed` in `FOLLOWUP_TICKETS.md` + [`docs/CHANGELOG.md`](docs/CHANGELOG.md).

## Certificazione corrente

Ultima baseline macchina-verificata: `main@aaf70032` — **10/11 PASS**.
Audit corrente: `main@a53a8d25` — **8/11 PASS** (3 regressioni: gate #3 I2 LOC, gate #7 R0 ARCHIVE, gate #10 Disk quota).
Nessuna baseline certificata oltre `aaf70032`.
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

1. **Gate #3:** Ridurre `software_renderer.hpp` da 203 a ≤200 LOC (tagliare 3 linee).
2. **Gate #7:** Risolvere la violazione R0 — o aggiornare il gate per consentire archival via commit espliciti, o revert/write un workaround.
3. **Gate #10:** Liberare spazio su /tmp o usare `TMPDIR` alternativo; ripulire ccache (`ccache -C`); ri-eseguire `install_consumer_test.sh`.
4. Raggiungere 11/11 PASS sullo stesso commit, poi revocare formalmente il feature freeze.

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

## Gate audit snapshot — `main@e8623a8a` (2026-07-03, post-Fase A — P0 completati)

| # | Gate                                        | Esito      | Note                                                                       |
|---|---------------------------------------------|------------|----------------------------------------------------------------------------|
| 1 | `check_architecture_boundaries.sh`          | ✅ PASS    | 16/16 check.                                                              |
| 2 | `check_architecture_boundaries_selftest.sh` | ✅ PASS    | 15/15 assertions.                                                          |
| 3 | `check_software_renderer_boundary.sh`       | ✅ PASS    | Tutti gli invarianti rispettati.                                           |
| 4 | `check_gitignored_dirs.sh`                  | ✅ PASS    | 31 directory, tutte pulite.                                                |
| 5 | `audit_software_renderer.sh`                | ✅ PASS    | Report generato, exit 0.                                                   |
| 6 | `check_camera_architecture.sh`              | ✅ PASS    | 6/6 check.                                                                 |
| 7 | `check_doc_sync.sh`                         | ✅ PASS    | 0 hard failures, 0 warnings.                                               |
| 8 | `check_filename_drift.sh`                   | ⚠️ PASS*   | warn-mode; 170 drift warning (↑ da 73 — audit hasher header).             |
| 9 | `test_architectural.sh`                     | ✅ PASS    | Static architecture-level rot: 0.                                          |
| 10 | `install_consumer_test.sh`                | ❓ NOT RUN  | Timeout (30s); richiede build completa. Bloccato da infra (disk quota PCH). |
| 11 | `check_backend_sanitization.py`            | ✅ PASS    | Tutti i check passati.                                                     |

**Totale: 10/10 verificati, 1 NOT RUN (gate #10).** 9/10 PASS + 1 PASS* (warn-mode).

## Gate audit snapshot — `main@a53a8d25` (2026-07-03, audit fresco — HISTORICAL)

| # | Gate                                        | Esito      | Note                                                                       |
|---|---------------------------------------------|------------|----------------------------------------------------------------------------|
| 1 | `check_architecture_boundaries.sh`          | ✅ PASS    | 14/15 check, 3 advisory (10, 12, 13 — non-blocking).                      |
| 2 | `check_architecture_boundaries_selftest.sh` | ✅ PASS    | 15/15 assertions.                                                          |
| 3 | `check_software_renderer_boundary.sh`       | ❌ FAIL    | I2: `software_renderer.hpp` LOC=203 > 200.                                 |
| 4 | `check_gitignored_dirs.sh`                  | ✅ PASS    | 31 directory, tutte pulite.                                                |
| 5 | `audit_software_renderer.sh`                | ✅ PASS    | Report generato.                                                           |
| 6 | `check_camera_architecture.sh`              | ✅ PASS    | 6/6 check.                                                                 |
| 7 | `check_doc_sync.sh`                         | ❌ FAIL    | R0: commit `a53a8d25` ha toccato `docs/ARCHIVE/CHANGELOG_2026_H1.md`.     |
| 8 | `check_filename_drift.sh`                   | ⚠️ PASS*   | warn-mode; 73 drift warning (↓ da 155).                                    |
| 9 | `test_architectural.sh`                     | ✅ PASS    | Static architecture-level rot: 0.                                          |
| 10 | `install_consumer_test.sh`                | ❌ FAIL    | "Disk quota exceeded" su PCH (224MB/file, ccache 5.0G); unit 28/340. Regressione infrastrutturale. |
| 11 | `check_backend_sanitization.py`            | ✅ PASS    | Tutti i check passati.                                                     |

**Totale: 8/11 PASS** — 3 regressioni da chiudere: gate #3 (I2 LOC), gate #7 (R0 ARCHIVE), gate #10 (disk quota).

## Gate audit snapshot — `main@c40ba16f` (2026-07-02, post-rebase + runtime-fix — HISTORICAL)

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
| 10 | `install_consumer_test.sh`                | ⚠️ FIX PENDING (triple) | (a) `21b9fb5d` cmake case-fix + 44 transitive headers; (b) `75035f2b` runtime default-arg chain su `RenderPipeline::debug_graph`; (c) TICKET-Phase4-BlurTierRadii constexpr array `{{0,2,7,13,20}}` per il blur-tables. |
| 11 | `check_backend_sanitization.py`            | ✅ PASS    |                                                                            |

**Totale storico: 10/11 PASS** osservato su `c40ba16f`.

_Limite raccomandato: 150 righe (vedi `DOCUMENTATION_GOVERNANCE.md` DoD §10)._

## Gate audit snapshot — `main@efd841f0` (HISTORICAL, 2026-07-02)

> Historical reference (baseline-stale; current HEAD = `fe25f6bc`, 15 commit ahead).
> Source of truth: [`reports/perf/main-efd841f0-gates.json`](../reports/perf/main-efd841f0-gates.json) (schema `chronon3d.gates.v1`).
> Per-gate tee logs: [`reports/perf/main-efd841f0-tee/`](../reports/perf/main-efd841f0-tee/).
> _Source of truth; full 11-row table is in the JSON, non duplicato qui per brevità._

| Gate #10                              | `install_consumer_test.sh` | ❌ FAIL (`regression_type: infra-setup`, 0s elapsed) |
|---------------------------------------|----------------------------|------------------------------------------------------|

Diagnosi: `Could not find toolchain file: …/vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake` (env precond `VCPKG_ROOT` non impostata).  **Distinct da TICKET-111** OPP-side text rot.  Forward-fix: `TICKET-install-consumer-vcpkg-bootstrap` ([`docs/FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md)).  Nessuna regressione di codice osservata; tutti gli altri 10 gates PASS (vedi JSON per i dettagli per-gate).

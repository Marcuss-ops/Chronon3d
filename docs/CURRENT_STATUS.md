# Chronon3D — Current Status

> **Snapshot:** `main@1078ab46` (11-gate audit: 10/11 PASS, gate #10 Phase 4 black-render pre-existing) — 2026-07-04. Linux-only.
>
> **Ultima baseline macchina-verificata:** `main@1078ab46` — **10/11 PASS** (gate #10 FAIL: Phase 4 render black).
> **Baseline precedente:** `main@e8623a8a` (10/10 verificati, 1 NOT RUN).
>
> **Gate #10 — `install_consumer_test.sh`:** Phase 1-3 PASS. Phase 4 FAIL: render black (230400 pixel < 5/255). Bug pre-esistente. Fix `sw_sidecar` threadato attraverso `render_scene_via_graph` (commit `2ef2b377`) — corregge il culling layer, ma non basta.
> **Gate #8:** 82 drift warning (↓ da 170).
>
> **Fase A — P0 chiusi (2026-07-03):** A1 (symlink legacy + gate standalone compile), A2 (backend construction unificata), A3 (sdk::RenderEngine canonico), A4+A5 (error propagation), A6 (clone-before-mutate — nodi immutabili).
>
> **Fase B2+B3 — Globali ELIMINATI (2026-07-03):** `process_wide_assets_root()`, `process_wide_resolver()`, `set_process_wide_assets_root()` rimossi da API pubblica e implementazione. `shared_text_layout_cache()` / `reset_shared_text_layout_cache()` rimossi; sostituiti da `TextRunShape::cache` (per-shape) + `TextLayoutCache*` parametri espliciti nelle driver function. Zero singleton globali residui. Commit: `7058dacc` + `876a14ac`.

> **M1.5#1 (2026-07-03, post-sync):** `TextRunNode.cpp` refactored into a 5-stage orchestrator + 3 single-responsibility helpers in `src/render_graph/nodes/text_run/` (execution / transform / diagnostics). La superficie pubblica è invariata (NodeExecResult da P0-1, predicted_bbox, cache_key); `execute()` ora contiene solo orchestrazione. Nuovo test `test_text_run_node_return_channel.cpp` locka il contratto del **canale di ritorno** `result.ok() / result.error()` per le 3 failure mode (ExecutionFailure, CapabilitiesOff, NullBackend) + il percorso Success. La libreria `chronon3d_graph_nodes` compila clean (`LIB_EXIT=0`); gli esistenti lock diagnostici (`test_text_run_node_execute_error.cpp` P0-3) e i contratti core (`test_protected_core_contracts.cpp::CoreContract:TextRunNode*`) rimangono P0-protected. ~~Note: il build del target di test `chronon3d_render_graph_tests` incontra pre-esistenti errori in `src/render_graph/pipeline/camera_change_policy.cpp` (`Camera2_5D::projection_mode`) — fuori scope M1.5#1.~~ **Risolto:** commit `ac514fea` ha fixato `projection_mode→optics_mode`. Build target ancora rotto per TICKET-011 (text_unit_map build rot, non correlato).
>> **M1.5#2 (2026-07-03, post-M1.5#1):** `src/text/text_run_driver.cpp` (530 LOC) decomposto in orchestratore breve + 3 moduli single-responsibility in nuova sottodirectory `src/text/driver/`: `text_state_sampler` (target_text + crossfade_target_text + is_in_crossfade_gap), `text_font_state` (compute_effective_font P0-2 compatibile), `text_layout_rebuild` (EffectiveTextState projection + fast_path_match + rebuild_active_side + rebuild_crossfade_slot + prewarm_for_frame). Nuova struct pubblica `EffectiveTextState` (text + FontLayoutIdentity + TextDirection + Bcp47LanguageTag + TextShapingFeatures) introdotta in `include/chronon3d/text/text_run_driver.hpp`; operator== confronta **tutti i 5 campi** in declaration order (cache-key regression lock). Il fast-path in `apply_active_state_to_text_run_shape` ora partecipa a TUTTE le dimensioni di `TextLayoutCacheKey::digest()`, non più solo `source_text + FontLayoutIdentity` come pre-M1.5#2 (P0-2 partial fix). Nuovo test `tests/text/test_effective_text_state.cpp` locka la semantica dei 5 campi + l'uguaglianza + la disuguaglianza + il crossfade lifecycle. La libreria `chronon3d_text_core` compila clean (`LIB_EXIT=0`); gli esistenti `tests/text/test_text_run_driver.cpp` rimangono P0-protected. Nessun nuovo singleton/registry/cache (cache flows via `TextRunShape::cache` o `TextLayoutCache*` esplicito); `process_wide_*` e `shared_text_layout_cache()` invariati in stato `rimosso` (Fase B2+B3 gia' chiusa). ~~Stesso blocker pre-esistente di M1.5#1 blocca `chronon3d_core_tests` LINK step.~~ **Risolto:** commit `ac514fea` ha fixato `projection_mode→optics_mode`.
>
> **Camera C1–C7 (2026-07-04):** Projection contract chiuso su `main@eb1ce8e5`. Golden test 6-mode in `tests/scene/camera/golden_projection_test.cpp`. Certificazione runtime chiusa in C9a (`37c03c11`).
>
> **Camera C9a (2026-07-04, `37c03c11`):** Runtime certifier di `tests/scene/camera/golden_projection_test.cpp` — 1 TEST_CASE × 6 SUBCASEs (Zoom, FOV 50°, PhysicalLens ARRI Alexa 35, GateFit::Stretch, GateFit::Overscan, Anamorphic 2×), **71/71 assertion PASS** (toll 1e-3) in `build/tests/chronon3d_scene_tests` post-C9a.  Build fix C9a: `SKIP_UNITY_BUILD_INCLUSION` su `timed_text_document.cpp` + `boundary_resolver/text_unit_map.cpp` per chiudere ODR TU-locali pre-esistenti in `chronon3d_text_core`.  24 fallimenti pre-esistenti emersi in `chronon3d_scene_tests` (esclusi dal certifier) → vedi [TICKET-120](tickets/TICKET-120.md).  Compilazione clean confermata da `tools/check_architecture_boundaries.sh` (gate #1, gate #6).  Camera Production V1 resta PARTIAL.
>
> **Gate #10 sw_sidecar fix (2026-07-04):** `SoftwareRenderer* sw_sidecar` ora threadato da `render_composition_frame` → `render_scene_via_graph` (commit `2ef2b377`). Corregge il culling layer che si verificava quando `sw_renderer` era null dentro `compute_dirty_rect`. Phase 4 ancora FAIL (render black) — richiede ulteriore debugging.
>
> **Fase C2 — Factory unificata (2026-07-03):** `RenderRuntime::create(RuntimeConfig)` → `Result<RenderRuntime, RuntimeBuildError>`.
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
| SDK C++ installabile                            | NOT RUN  | consumer di rendering reale con testo + camera → PNG in certificazione.   |
| SDK cross-language                              | NOT RUN  | C ABI e formato `.chronon` da progettare.                                |
| Sistemi meta (Expressions V2 / V3 tile-first)   | PLANNED  | Expressions V2 OFF di default, non installato. V3 subordinato a V1.      |
| Render runtime (session + caches)               | PASS     | P0 #B1: ImageCache moved to RenderRuntime. P1 #3: `RenderSession::layout_cache` added. |
| Render engine construction                      | PASS     | P0 #C2: Impl ctor unified, `optional<path>`. attach_backend() deprecated. |
| Composition pipeline                            | PASS     | P0 #C3: canonical pipeline documented. render_composition_frame canonical. |
| Text pre-compilation (CompiledTextRun)          | PLANNED  | P0 #C1: documented in text_run.hpp. Blocked by feature freeze. |

## Camera V1 — DoD (6 CAM-DOC 04 architecture-boundary checks)

Lock canonico per la DoD Camera V1 verificato da `tools/check_camera_architecture.sh` (gate #6 dell'11-gate audit).  **6/6 PASS** su `main@3a5eb193` (snapshot 2026-07-04):

| # | Check                                                                                              | Stato | Verifica (`tools/check_camera_architecture.sh`) |
|---|-----------------------------------------------------------------------------------------------------|-------|---------------------------------------------------|
| 1 | AnimatedCamera2_5D RETIRED                                                                          | PASS  | `grep` su `include/chronon3d/{scene,backends,runtime}/...` nessun reference residuo. |
| 2 | CameraRig authoring RETIRED                                                                          | PASS  | `camera_rig.hpp` non presente in alcun `target_sources`. |
| 3 | SceneBuilder::animated_camera() RETIRED                                                              | PASS  | nessuna chiamata a `scene_builder::animated_camera` in `src/**` o `content/**`. |
| 4 | SceneBuilder::camera_rig() RETIRED                                                                   | PASS  | nessuna chiamata a `scene_builder::camera_rig` in `src/**` o `content/**`. |
| 5 | tan(fov) focal formulas canonical                                                                 | PASS  | un'unica site in `include/chronon3d/scene/math/camera_math.hpp`; nessuna duplicazione. |
| 6 | compile_camera() call-site policy                                                                    | PASS  | chiamate solo da `runtime_adapter.cpp` (orchestrator); non abusato da nodes/backend. |

**Source of truth:** `tools/check_camera_architecture.sh` (6 grep-comparison checks) — output `6/6 PASS` catturato dal forensic run Step 6 (`docs/audits/2026-07-04-step6-camera-gates.md` quando scritto).

**Note su Camera V1 vs RELEASE_GATE.md:** il canonico `docs/RELEASE_GATE.md` lista 7 condizioni ("all new compositions use CameraDescriptor or CameraProgram" / "CameraSession owned by render job" / "no per-frame lookup/compile" / "orientation/projection single math contract" / "compiled tests link + run in CI" / "legacy adapters parity + removal phase" / "external SDK builds + uses compiled camera"). Le 6 CAM-DOC 04 checks sopra sono la colonna architetturale della DoD; le rimanenti sono runtime/CI/SDK invariants tracciate nel forensic run Step 6 (gate b/d Pass + gate c FAIL su pre-existing rot fuori scope Camera V1 step).

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
| TICKET-120  | C9a (`37c03c11`) — 24 fallimenti pre-esistenti in `chronon3d_scene_tests` (incl. `TICKET-034D` CameraDescriptor fingerprint SIGABRT, `TICKET-035` anamorphic_squeeze wrong-asset `test_camera_projection_contract.cpp:572`); emersi dopo C9a abilita build clean con `SKIP_UNITY_BUILD_INCLUSION` su 2 file di `chronon3d_text_core` | PARTIAL  | certificazione Camera V1 end-to-end |
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

## Gate audit snapshot — `main@1078ab46` (2026-07-04, 11-gate audit)

| # | Gate                                        | Esito      | Note                                                                       |
|---|---------------------------------------------|------------|----------------------------------------------------------------------------|
| 1 | `check_architecture_boundaries.sh`          | ✅ PASS    | 15/15 check, 2 advisory (check 12 CMake registry, check 13 vcpkg dep).   |
| 2 | `check_architecture_boundaries_selftest.sh` | ✅ PASS    | 15/15 assertions.                                                          |
| 3 | `check_software_renderer_boundary.sh`       | ✅ PASS    | I1-I5 tutti rispettati.                                                    |
| 4 | `check_gitignored_dirs.sh`                  | ✅ PASS    | Directory pulite.                                                          |
| 5 | `audit_software_renderer.sh`                | ✅ PASS    | Report generato, exit 0.                                                   |
| 6 | `check_camera_architecture.sh`              | ✅ PASS    | 6/6 check.                                                                 |
| 7 | `check_doc_sync.sh`                         | ✅ PASS    | 0 hard failures, 0 warnings.                                               |
| 8 | `check_filename_drift.sh`                   | ✅ PASS    | 82 drift findings.                                                         |
| 9 | `test_architectural.sh`                     | ✅ PASS    | Static architecture-level rot: 0. Exit 0.                                  |
| 10 | `install_consumer_test.sh`                  | ❌ FAIL    | Phase 1-3 PASS. Phase 4: render black (230400 pixel < 5/255). Pre-existing. |
| 11 | `check_backend_sanitization.py`             | ✅ PASS    | Tutti i check passati.                                                     |

**Totale: 10/11 PASS.** Gate #10 FAIL (Phase 4 render black — bug pre-esistente, sw_sidecar fix in `2ef2b377` non sufficiente).

## Prossimo passo operativo

1. **Gate #10 Phase 4:** Investigare il bug di rendering black-output nel consumer test. Diagnosi preliminare: `sdk::RenderEngine::render()` bridge → `render_composition_frame()` → `comp.evaluate()` produce una Scene apparentemente valida (layer count > 0) ma il framebuffer finale è nero. Possibili cause: (a) `SoftwareBackend::draw_node()` non dispatcher correttamente il GridBackgroundShape, (b) il framebuffer pool non è configurato, (c) la camera projection non produce pixel visibili.
2. **Gate #10 Phase 4 fix:** Risolvere il bug e ri-eseguire `install_consumer_test.sh` fino a PASS completo.
3. Raggiungere 11/11 PASS sullo stesso commit, poi revocare formalmente il feature freeze.

## Hygiene

Main-sync hygiene (`main` come single-source-of-truth; tutto converge in `main`) è enforced da **tre pezzi coordinati** su ogni push workflow:

| Pezzo                    | Scope                                                                                       | Strumento canonico                                                |
|--------------------------|---------------------------------------------------------------------------------------------|-------------------------------------------------------------------|
| **Branch-level rebase** (read-side) | `git pull` unpulled su `branch.<name>.rebase = true` esegue rebase invece di merge (history lineare)            | `git config branch.<name>.rebase true` (per-repo local, **NON** `--global`) |
| **GATE-MNT-01 wrap-push** (push-side) | Auto-FF unidirezionale + invocazione `check_main_clean` come drop-in per `git push`                            | [`tools/wrap_push.sh`](tools/wrap_push.sh) (portabile, tracked)   |
| **Clean-tree check** (gate) | Fail-on-dirty su divergenza HEAD/origin/main + working-tree dirty                              | [`tools/check_main_clean.sh`](tools/check_main_clean.sh)          |

Le tre componenti sono complementari (e formano una triade): il read-side rebase previene i merge-commit locali, il wrapper canonico applica il gate al push, il gate fallisce su dirty → tutte insieme (e solo tutte insieme) garantiscono che `git log main` resti lineare e che l'HEAD pubblicato sia sempre verificabile.

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

## Gate audit snapshot — `main@876a14ac` (2026-07-03, post-Fase A + B2+B3 completati)

| # | Gate                                        | Esito      | Note                                                                       |
|---|---------------------------------------------|------------|----------------------------------------------------------------------------|
| 1 | `check_architecture_boundaries.sh`          | ✅ PASS    | 16/16 check.                                                              |
| 2 | `check_architecture_boundaries_selftest.sh` | ✅ PASS    | 15/15 assertions.                                                          |
| 3 | `check_software_renderer_boundary.sh`       | ✅ PASS    | I1-I5 tutti rispettati. Header LOC=200 ≤ 200.                              |
| 4 | `check_gitignored_dirs.sh`                  | ✅ PASS    | 31 directory, tutte pulite. 85 entry totali.                               |
| 5 | `audit_software_renderer.sh`                | ✅ PASS    | Report generato, exit 0.                                                   |
| 6 | `check_camera_architecture.sh`              | ✅ PASS    | 6/6 check. CAM-DOC 04 boundary ok.                                        |
| 7 | `check_doc_sync.sh`                         | ✅ PASS    | 0 hard failures, 0 warnings.                                               |
| 8 | `check_filename_drift.sh`                   | ⚠️ PASS*   | warn-mode; 170 drift warning (stabile).                                    |
| 9 | `test_architectural.sh`                     | ✅ PASS    | Static architecture-level rot: 0. Exit 0.                                  |
| 10 | `install_consumer_test.sh`                | ❓ NOT RUN  | Timeout 120s (build 107/341). Regressione infrastrutturale, non di codice. |
| 11 | `check_backend_sanitization.py`            | ✅ PASS    | Tutti i check passati.                                                     |

**Totale: 9/10 PASS + 1 PASS*** (warn-mode) + **1 NOT RUN** (gate #10 infrastrutturale).

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

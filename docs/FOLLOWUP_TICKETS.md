# Follow-up Tickets — Open Blockers Index

> Stato corrente: [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md).
> Dettaglio completo di ogni ticket: [`docs/tickets/TICKET-NNN.md`](docs/tickets/).
> Cronologia completa ticket chiusi + deferred: [`docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`](docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md).

## Open blockers (prioritari per baseline verde)

Solo ticket realmente aperti (PLANNED / PARTIAL). Nessun DONE in questa sezione.

| ID | Pri | Area | Stato | Blocca | Scheda |
|---|---|---|---|---|---|
| TICKET-036 | P0 | camera architecture gate | PLANNED | arch-boundary gate 5/6 | [TICKET-036.md](tickets/TICKET-036.md) |
| TICKET-011 | P0 | mainline build rot | PARTIAL | arch-boundary gate 1–8 (cron3d_core_tests rot multi-file eterogeneo, non solo claim iniziale) | [TICKET-011.md](tickets/TICKET-011.md) |
| TICKET-005 | P1 | post-cascade cleanup | PARTIAL | arch-completeness gate 5 | [TICKET-005.md](tickets/TICKET-005.md) |
| ~~TICKET-022~~ | ~~P1~~ | ~~camera double look-at compiled path~~ | ~~PARTIAL~~ | ~~arch-boundary gate 5/6~~ | [TICKET-022.md](tickets/TICKET-022.md) — **chiuso (Recently closed)** |
| TICKET-044 | P1 | selftest hardcoded paths | PLANNED | arch-boundary gate 5 | [TICKET-044.md](tickets/TICKET-044.md) |
| TICKET-046 | P1 | filename drift stale references | PLANNED | arch-boundary gate 5 | [TICKET-046.md](tickets/TICKET-046.md) |
| TICKET-051 | P1 | per-preset visual diagnostic | PLANNED | A4.3 visual gate | [TICKET-051.md](tickets/TICKET-051.md) |
| TICKET-080 | P1 | `install_consumer_test.sh` — vcpkg toolchain path relativa al worktree (`<wt>/vcpkg_bootstrap/...`); regressione `efd841f0` con `VCPKG_ROOT` non impostata (`regression_type: infra-setup`) | PLANNED | gate #10 (env precond) | [TICKET-080.md](tickets/TICKET-080.md) |
| TICKET-064 | P1 | ExecutionScope error model (ScopeError/ScopeErrorCode) | PARTIAL | arch-boundary gate 5 | [TICKET-064.md](tickets/TICKET-064.md) |
| TICKET-120 | P1 | C9 — 24 fallimenti pre-esistenti in `chronon3d_scene_tests` (incl. `TICKET-034D` CameraDescriptor fingerprint-serializable SIGABRT in `test_composition_default_camera.cpp:69`; `TICKET-035` anamorphic_squeeze wrong assertion in `test_camera_projection_contract.cpp:572` `CHECK(2666.67 == Approx(2250))`); emersi dopo C9 abilita build clean con `SKIP_UNITY_BUILD_INCLUSION` su `timed_text_document.cpp` + `boundary_resolver/text_unit_map.cpp` (ODR TU-locali pre-esistenti) — **Cat-1 progress: 3/24 chiusi** (commit `7ee302bf` 3 Agent3-introduced test-compile regression FocalPx/FrameRate/CameraSession + commit `4edfda5c` size()→points().size() in `camera_program_compiler.cpp:331` + commit `8b29a5bf` Cat-1 seed) | PARTIAL | certificazione Camera V1 end-to-end | [TICKET-120.md](tickets/TICKET-120.md) |
| TICKET-GATE-7-R1 | P1 | `src/runtime/**` non documentato in `CURRENT_STATUS.md` + `ROADMAP.md` | PLANNED | gate #7 doc-sync | — |
| TICKET-GATE-4-LEAK | P1 | `reports/perf/main-73a2aa9b-gates.json` tracked con abs-path leak | PLANNED | gate #4 gitignored-dirs | — |
| TICKET-GATE-10-PHASE-4-FIX | P0 | `install_consumer_test.sh` Phase 4 — `ninja subcommand failure during compilation of highway_*_kernels.cpp.o` in `chronon3d_backend_software` target (carry-over rot da `9ecb4879`; failure-mode shift da Phase 4 PNG near-black a `c73f7493`); diagnostica preliminare: SKIP_UNITY_BUILD_INCLUSION side-effect da `cc48f3ee` FASE 2.2 catch2→doctest o vcpkg dep coverage gap | PLANNED | gate #10 install_consumer (revoca feature freeze) | — |
| TICKET-GATE-11-PRINTF-FIX | P0 | `src/backends/software/processors/software_grid_background_processor.cpp:23` — bare `printf` (intro `b62ef4429`, pre-esistente on `main`); sostituire con `chronon3d_log::log_info` o API canonica `diagnostic_chronicler` | PLANNED | gate #11 backend sanitization (revoca feature freeze) | — |

Ordinamento: priorità gate-impact desc (P0 > P1), poi per ID.
## Agent3 — Compiler, Session, Errori, Legacy Cleanup (PLANNED, cluster)

Workstream design-FROZEN 2026-07-04 contro feature-freeze attivo; **nessun codice toccato**, solo ticket-schema. Origini: prompt operativo Agent3 per `camera_v1` compiler/session/cache + framerate propagation + LookAtLayer diagnostic + ADR. Ogni gate-DoD è testabile da `tests/scene/camera/test_camera_program_compiled.cpp` + `tests/runtime/test_camera_session.cpp` post-freeze. Matrice DoD (gate → ticket):

| DoD gate | Requisito | Ticket |
|---|---|---|
| (a) | `RegisteredMotionRef` non eredita metadata dal preset referenziato | TICKET-A3-METADATA |
| (b) | `DampedFollowConstraint` impone sempre `RequiresHistory` | TICKET-A3-DAMPED-HISTORY |
| (c) | `CameraFailurePolicy::KeepLastValidCamera` davvero usa l'ultima camera valida | TICKET-A3-SESSION-POLICY |
| (d) | un'evaluation fallita NON aggiorna la session cache | TICKET-A3-CACHE-LEASE |
| (e) | `CameraEvalContext::at()` non ha più framerate hardcoded | TICKET-A3-CTX-FRAMERATE |
| (f) | `pre-roll` riceve frame rate esplicito | TICKET-A3-PRE-ROLL-FPS |
| (g) | `LookAtLayer` senza `transforms` → diagnostic, non silenzio | TICKET-A3-LOOKAT-DIAGNOSTIC |
| (h) | ADR legacy deletion ha ticket concreti collegati al codice | TICKET-A3-ADR-013 (ponte verso [ADR-011](./adr/ADR-011-camera-legacy-deletion.md)) |

| ID | Pri | Area | Stato | Note (scope residuo post-freeze) |
|---|---|---|---|---|
| TICKET-A3-METADATA        | P1 | `camera_v1::compile_camera` — rimuovere `return` anticipato dopo graft del preset-referenziato; late-rebuild `failure_policy_/time_dependent_/evaluation_dependency_` | PLANNED | header OK (`CameraCompileError::CircularCatalogReference` esiste già); body `src/scene/camera/camera_v1/camera_program_compiler.cpp` ricompilazione post-graft ancora da fare |
| TICKET-A3-SESSION-POLICY  | P1 | `CameraFailurePolicy::KeepLastValidCamera` realmente recupera `CameraSession::last_valid_camera` (oggi segue il ramo di `Stop`) | PLANNED | field `last_valid_camera` già presente in `camera_session.hpp`; decision-table in body ancora cablata su Stop |
| TICKET-A3-CACHE-LEASE     | P1 | `CameraSessionCache::acquire()` non anticipa `last_evaluated_frame`; lease `commit()` on success only; failed evaluation non muta cache | PLANNED | `CameraSessionLease` RAII già presente in `camera_session_cache.hpp`; integrità commit/rollback ancora da verificare nel body |
| TICKET-A3-CTX-FRAMERATE   | P1 | `CameraEvalContext::at(Frame, FrameRate)` + `CameraMotionContext::at(Frame, FrameRate)` audit call-site per residui `FrameRate{30,1}` hardcoded | PLANNED | factory canonica già presente in `camera_motion_context.hpp`; audit call-site per default-nascosti |
| TICKET-A3-PRE-ROLL-FPS    | P1 | `preroll_session_for_frame(...)` riceve `FrameRate` esplicito, senza default | PLANNED | signature già parametrizzata su `frame_rate` in `camera_session_cache.hpp`; verificare che ogni call-site passi fps non-default |
| TICKET-A3-DAMPED-HISTORY  | P1 | `DampedFollowConstraint` forza sempre `ProgramKind::RequiresHistory` nel compiler | PLANNED | policy dichiarata in `camera_constraint.hpp`; compilatore deve applicarla in metadata classification |
| TICKET-A3-LOOKAT-DIAGNOSTIC | P1 | `LookAtLayer` senza `transforms` (`CameraEvalContext::transforms == nullptr`) emette `CameraDiagnostic{Code::MissingTransforms, Severity::Warning}`, no silenzio | PLANNED | `CameraEvalContext::transforms{nullptr}` esiste; emissione strutturata da aggiungere al path di evaluation |
| TICKET-A3-ADR-013         | P1 | ADR-013 (camera_v1 semantics + framerate explicit + cache-lease contract) + ticket-to-code chain verso [ADR-011](./adr/ADR-011-camera-legacy-deletion.md) | PLANNED | doc-only |

**Overlap / distinzioni importanti** (leggere prima di lavorare):

- I ticket A3-* sono **semantic-level** (compiler/session/cache contract), **non** il **type-level deletion** di [ADR-011](./adr/ADR-011-camera-legacy-deletion.md) Decisions 1–5 (AnimatedCamera2_5D / dual CameraRig / CameraShotProfile / camera_descriptor_adapters / Camera2_5D::projection_mode field). Quest'ultimo è parzialmente chiuso (`projection_mode` già rimosso in `camera_2_5d.hpp`).
- `TICKET-camera-policy-pre-existing` (rot pre-esistente in `src/render_graph/pipeline/camera_change_policy.cpp:24`) resta fuori scope del cluster A3 e si appoggia alla migration list di ADR-011 Decision 5.
- `P1 #10 Frame rate hardcoded` (DONE `6df9b429`+`560750e3`) ha coperto gli overload `render_scene` globali; `TICKET-A3-CTX-FRAMERATE` è scope narrower (factory `camera_v1::*Context::at()`).

**Avvio rigido (per AGENTS.md "avvio rigido: prima mini-sequenza 1→2→3→4"):**
`A3-METADATA → A3-SESSION-POLICY → A3-CACHE-LEASE → A3-CTX-FRAMERATE → A3-PRE-ROLL-FPS → A3-DAMPED-HISTORY → A3-LOOKAT-DIAGNOSTIC`, `A3-ADR-013 last`.

**Vincoli architetturali di esecuzione:**

- un commit per ticket; **direttamente su `main`**, no branch;
- `tools/wrap_push.sh origin main` prima di ogni push (GATE-MNT-01);
- aggiornare `CURRENT_STATUS.md` + `FOLLOWUP_TICKETS.md` nello stesso commit (gate #7 `check_doc_sync.sh`);
- `tools/check_camera_architecture.sh` deve restare 6/6 PASS dopo ogni commit;
- nessun **nuovo** singleton/registry/resolver/cache/service-locator (regola permanente).

## Partial closures (lavoro iniziato, non ancora DONE)

| ID | Pri | Area | Stato | Dettaglio |
|---|---|---|---|---|
| TICKET-070 | P1 | SoftwareBackendServices validation | PARTIAL | Validazione dipendenze backend incompleta |
| TICKET-P1-PART2 | P1 | P1 census #7–#12 (6 issues) | PARTIAL | #10 e #12 DONE; #7, #8, #9, #11 PLANNED |
| TICKET-PUBLIC-MANIFEST-03 | P1 | OPP-cpp incomplete-type in `software_backend.cpp` | PARTIAL | Fix (a) applicato in `ac5f7125`; OPP compile end-to-end ancora da verificare |

## P1 backlog (post-baseline, PLANNED)

| ID | Pri | Area | Stato | Scheda |
|---|---|---|---|---|
| TICKET-P1-07 | P1 | Asset resolver globale | PLANNED | [TICKET-P1-07.md](tickets/TICKET-P1-07.md) |
| TICKET-P1-08 | P1 | Text renderer monolite | PLANNED | [TICKET-P1-08.md](tickets/TICKET-P1-08.md) |
| TICKET-P1-09 | P1 | RenderRuntime service locator | PLANNED | [TICKET-P1-09.md](tickets/TICKET-P1-09.md) |
| TICKET-P1-11 | P1 | Timeline percorsi multipli | PLANNED | [TICKET-P1-11.md](tickets/TICKET-P1-11.md) |
| TICKET-render-pipeline-fps-defaults-audit | P1 | audit `render_scene` overloads + free-fun `float fps` params | PLANNED | code-review hygiene, non-blocking |

**P1 progress summary (2026-07-04):**
- P1 #1–#5: **DONE** (Part 1 completata, commit `0892a224`)
- P1 #10 (Frame rate hardcoded): **DONE** (commit `6df9b429` + `560750e3`)
- P1 #12 (CMake fragile): **DONE** (commit `59b2439f`)
- P1 #7, #8, #9, #11: **PLANNED** (post-baseline)

## In progress — fix pushed, pending CI verification

| ID | Area | Stato | Note |
|---|---|---|---|
| Gate-10 Phase 4 (CMake) | consumer-SDK CMake case-fix (TBB/xxHash title-case) + 44 transitive headers → `cmake/Chronon3DPublicHeaders.cmake` (commit `21b9fb5d`) | FIX PUSHED | CI machine-verified Phase 4 ancora pending |
| Gate-10 Phase 4 (Runtime) | `RenderPipeline::debug_graph` default-arg chain: `float fps = 0.0f` sentinel (commit `75035f2b` → `c40ba16f` post-rebase) | FIX PUSHED | Phase 4 CI end-to-end pending |
| TICKET-GATE-10-PHASE-4-FULL | 15 missing non-internal transitive headers (vendored glm/tracy/magic_enum wrappers); first hit: `detail/framebuffer_impl.hpp` | FIX PUSHED | Incremental batch-add a `cmake/Chronon3DPublicHeaders.cmake` |
| TICKET-Phase4-BlurTierRadii | `static constexpr std::array<i32, 5> kBlurTierRadii = {{0, 2, 7, 13, 20}};` ripristinato in `text_run_processor.cpp` | FIX PUSHED | Gate #10 Phase 4 |
| TICKET-GATE-10-PHASE-4 | consumer-build Phase 4: triple fix (a) cmake case-fix, (b) runtime default-arg, (c) BlurTierRadii restore | FIX PUSHED | CI confirm ancora pending |

## Recently closed (DONE — verificati, giugno–luglio 2026)

| ID | Area | Commit / Note |
|---|---|---|
| TICKET-P1 | P1 census (5 issues, done): text/cache/cmake | commit `0892a224` |
| TICKET-GATE-1+9-SSOT-POSIX-REGEX | `tools/check_architecture_boundaries.sh` Check 16 — mawk-incompat `\s`/`\S` regex migrato a POSIX `[[:space:]]`/`[^[:space:]]` (3 awk blocks); riabilitava gate #1 + gate #9 Check 16 SDK public-deps SSoT wiring assertion su sistemi con `mawk` (default Debian/Ubuntu) | commit `a5ee07e7`; 2/11 flipped FAIL→PASS (g1: `[16/16] SDK public-deps SSoT wiring ... PASS (registry=7 entries; marker=1 substitution / 0 hand-written)`; g9: `architecture-boundary child_target_check_arch_boundary (12 checks) PASS`); rebased clean su origin/main (no file overlap con `cc48f3ee` + `34289560`) |
| TICKET-GATE-MNT-01-EXT | `tools/check_main_clean.sh` Step 4 + `tools/wrap_push.sh` Step 2.5 + `tools/install_consumer_test.sh` Step 0 + `AGENTS.md` §GATE-MNT-01 — per-branch rebase invariant verify + auto-repair (`branch.${TARGET_BRANCH}.rebase = true` se mancante) | commit `c73f7493`; new invariant (idempotent + forward-only); non cambia gate count ma rende il rebase invariant bloccante al gate failure; smoke-test delete→fail + restore→pass + auto-repair→pass verificato end-to-end |
| TICKET-camera-policy-pre-existing (M1.5#1 + M1.5#2 carryover) | `src/render_graph/pipeline/camera_change_policy.cpp:24` — `Camera2_5D::projection_mode` rot rimossa (sostituita con `Camera2_5D::optics_mode`); riabilitava build di `chronon3d_render_graph_tests` (M1.5#1) e `chronon3d_core_tests` (M1.5#2) | commit `ac514fea` (origin fix, projection_mode→optics_mode) + verified-clean su `main@83e74169` (`grep -rnE 'Camera2_5D::projection_mode' src/` → 0 hit + `prev->optics_mode` + `current.optics_mode` canonici in `camera_change_policy.cpp`); `tools/test_architectural.sh` + `tools/check_architecture_boundaries.sh` script-exit OK (carry-over FAIL su SDK public-deps SSoT Check 16 = pre-esistente gate-10 lineage, non introdotto da questo commit) |
| TICKET-P1-10 | Frame rate hardcoded | commit `6df9b429` + `560750e3` |
| TICKET-P1-12 | CMake fragile (ar merge + include_private) | commit `59b2439f` |
| M1.5#1 | TextRunNode.cpp orchestratore + 3 helpers (`text_run/`) + return-channel test | commit `82d2b0e0` |
| M1.5#2 | text_run_driver.cpp orchestratore + 3 helpers (`src/text/driver/`) + `EffectiveTextState` | commit `e837e274` |
| M1.5#3 | text_run.hpp umbrella split into 5 sub-headers (`text_layout_identity` / `text_run_layout` / `text_layout_cache` / `text_run_shape` / `text_run_hash`); Fase B3 deprecated singleton symbols (`shared_text_layout_cache` / `reset_shared_text_layout_cache`) rimossi da public API surface; lock contract test `tests/text/test_text_run_umbrella_contract.cpp` (static_assert-driven, cat-2 freeze-compliant, no Blend2D / no font engine dependency) | commit `843dc863` (refactor) + this-commit (lock test + cmake entry + doc sync) |
| TICKET-021 | Camera V1: PoseTracksSource variance-preserving dispatch | commit `82d2b0e0` (post-sync) + §2.A lock-tests|
| TICKET-022 | Camera V1: single-application canonical-order lock (orientation + constraint) + 4-camera-trajectory-lookat compiled path covered by §4.B.3 Single-Look-At Policy + §4.B.2 Canonical-order Application | commit `82d2b0e0` (post-sync) + Step 4+5 trajectory work + §4.B.1-§4.B.3 lock-tests in `test_camera_program_compiled.cpp` |
| TICKET-024 | Camera V1: orbit position math in camera-local basis | commit `82d2b0e0` (post-sync) + §4.C lock-tests |
| TICKET-025 | Camera V1: OrientAlongPath semantic correctness (tangent/roll/keep_horizon/degenerate-tangent) | Step 4 + `test_orient_along_path.cpp` lock-tests |
| TICKET-077 | gate-3 I2: `software_renderer.hpp` LOC 223→182 | — |
| TICKET-079 | gate-3 I5: `attach_software_backend` SWRenderer& → SWRenderer* | commit `ac5f7125` |
| TICKET-078 | gate-3 I3: non-local includes 7→6 | — |
| TICKET-087 | gate-3 I3: build-fix preflight_fonts | — |
| TICKET-106 | gate-3 I5: path-list-parity regression test | — |
| TICKET-086 | thread_local cache → static + mutex | — |
| TICKET-088 | HarfBuzz buffer pool in FontEngine | — |
| TICKET-090 | draw_text_run private stages refactor | — |
| TICKET-090b | draw_text_run stage-marker comments | — |
| TICKET-100 | eliminate legacy materialize_text_run_shape | — |
| TICKET-101 | compile_text_layout (TextDocument, paragraph_index) | — |
| TICKET-092 | per-paragraph error accumulator | — |
| TICKET-103a | TextLayoutRequest direction/language/features | — |
| TICKET-104 | PendingTextRun consumed real-decrement | — |
| TICKET-105 | identity/preservation regression test suite | — |
| TICKET-107 | per-category register helpers promoted | — |
| TICKET-108a | aliases.hpp 7 typedefs adopted | — |
| TICKET-109 | TextRunBuilderInspector + migrate 53 callsites | — |
| TICKET-098 | text_preset_registry per-category helpers | — |
| TICKET-099a | internal-only domain type aliases | — |
| TICKET-077b | std::call_once elimination (6 files) | — |
| TICKET-076 | wrap_push.sh auto-FF merge before gate | — |
| TICKET-075 | GATE-MNT-01 strict-SHA → ancestor-relation | — |
| TICKET-069 | FreeTypeFaceCache destruction contract | — |
| TICKET-074 | remove dead glyph_selector_v2 pipeline | — |
| TICKET-068 | crossfade+stroke regression test | — |
| TICKET-118 | SoftwareBackend::draw_node real impl + drop dummy TextRunProcessor | — |
| TICKET-119 | SoftwareBackend m_owner back-pointer removal + ProcessorSourceExtras bridge | — |
| P0-1 | RenderGraphNode::execute() → Result<OwnedFB, NodeExecutionError> (36 file) | — |
| P0-2 | FontLayoutIdentity unificata su cache/hash/fastpath/prewarm (5 file) | — |
| TICKET-PUBLIC-MANIFEST-01 | CMake public-manifest sed-artefact repair | commit `59db2da5` |
| TICKET-PUBLIC-MANIFEST-02 | OPP-include-path cascade closure (22 OPP-internal + 1 PUBLIC + 1 moved + 1 python-test) | 2026-07-02 |
| TICKET-render-session-cpp-brace | OPP-side `render_session.cpp` missing namespace-closing `}` | 2026-07-02 |
| Gate-10 build rot | consumer SDK + install_consumer_test Phase 1-3 verde | commit `ac5f7125` |
| TICKET-067 | GATE-MNT-01 divergence fix | — |
| TICKET-096 | stash@{0} debt triage (DROP 2 retro-fixup) | — |
| TICKET-080-unicode | unicode extraction utf8_decoder + whitespace (distinto da TICKET-080 vcpkg toolchain) | — |
| P1-CMAKE-01 | CMake core/video boundary fix (P1 #5) | — |

## Deferred / non-blocking (PLANNED, post-baseline)

| ID | Area | Note |
|---|---|---|
| _(nessun carry-over residuo: TICKET-camera-policy-pre-existing promosso a Recently closed 2026-07-04)_ | | |

## Altri ticket aperti

Tutti i ticket aperti non nella top-10 (TICKET-024, TICKET-026, TICKET-066, TICKET-078-deferred, TICKET-081a–116) sono tracciati in [`docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`](docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md). Le specifiche di accettazione complete sono nei file [`docs/tickets/`](docs/tickets/) individuali.

Cronologia completa: [`docs/CHANGELOG.md`](docs/CHANGELOG.md) e [`docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`](docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md).

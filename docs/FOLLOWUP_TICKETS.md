# Follow-up Tickets — Open Blockers Index

> Stato corrente: [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md).
> Dettaglio completo di ogni ticket: [`docs/tickets/TICKET-NNN.md`](docs/tickets/).
> Cronologia completa ticket chiusi + deferred: [`docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`](docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md).

## Open blockers (prioritari per baseline verde)

| ID | Priorità | Area | Stato | Blocca | Scheda |
|---|---|---|---|---|---|
| TICKET-036 | P0 | camera architecture gate | PLANNED | arch-boundary gate 5/6 | [TICKET-036.md](tickets/TICKET-036.md) |
| TICKET-011 | P0 | mainline build rot | PLANNED | arch-boundary gate 1–8 | [TICKET-011.md](tickets/TICKET-011.md) |
| TICKET-005 | P1 | post-cascade cleanup | PARTIAL | arch-completeness gate 5 | [TICKET-005.md](tickets/TICKET-005.md) |
| TICKET-022 | P1 | camera double look-at | PARTIAL | arch-boundary gate 5/6 | [TICKET-022.md](tickets/TICKET-022.md) |
| TICKET-044 | P1 | selftest hardcoded paths | PLANNED | arch-boundary gate 5 | [TICKET-044.md](tickets/TICKET-044.md) |
| TICKET-046 | P1 | filename drift stale refs | PLANNED | arch-boundary gate 5 | [TICKET-046.md](tickets/TICKET-046.md) |
| TICKET-051 | P1 | per-preset visual diagnostic | PLANNED | A4.3 visual gate | [TICKET-051.md](tickets/TICKET-051.md) |
| TICKET-080 | P1 | `install_consumer_test.sh` richiede `vcpkg.cmake` toolchain via path relativa al worktree (`<wt>/vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake`); em-dash su commit history `efd841f0` con `VCPKG_ROOT` non impostata (`regression_type: infra-setup`, code path mai esercitato) | PLANNED | `install_consumer_test.sh` gate #10 (env precond) | [TICKET-080.md](tickets/TICKET-080.md) |
| TICKET-064 | P1 | ExecutionScope error model | PARTIAL | arch-boundary gate 5 | [TICKET-064.md](tickets/TICKET-064.md) |

| TICKET-P1 | P1 | P1 census (5 issues, done) | DONE | post-baseline text/cache/cmake | [TICKET-P1-ACTION-PLAN.md](tickets/TICKET-P1-ACTION-PLAN.md) |
| TICKET-P1-PART2 | P1 | P1 census #7–#12 (6 issues) | PARTIAL | asset/cache/runtime/framerate/timeline/cmake | [TICKET-P1-ACTION-PLAN-PART2.md](tickets/TICKET-P1-ACTION-PLAN-PART2.md) |
| TICKET-P1-07 | P1 | Asset resolver globale | PLANNED | asset | [TICKET-P1-07.md](tickets/TICKET-P1-07.md) |
| TICKET-P1-08 | P1 | Text renderer monolite | PLANNED | text | [TICKET-P1-08.md](tickets/TICKET-P1-08.md) |
| TICKET-P1-09 | P1 | RenderRuntime service locator | PLANNED | runtime | [TICKET-P1-09.md](tickets/TICKET-P1-09.md) |
| TICKET-P1-10 | P1 | Frame rate hardcoded | DONE | render | [TICKET-P1-10.md](tickets/TICKET-P1-10.md) |
| TICKET-P1-11 | P1 | Timeline percorsi multipli | PLANNED | timeline | [TICKET-P1-11.md](tickets/TICKET-P1-11.md) |
| TICKET-P1-12 | P1 | CMake fragile | DONE | cmake | [TICKET-P1-12.md](tickets/TICKET-P1-12.md) |

**P1 progress summary (2026-07-02):**
- P1 #1–#5 DONE (Part 1 completata, commit `0892a224`)
- P1 #7–#12 PLANNED (Part 2 censita, commit `8976908a`): asset globale, text monolite, RenderRuntime service locator, frame rate hardcoded, timeline percorsi multipli, CMake fragile

Ordinamento: priorità gate-impact desc (P0 > P1), poi per ID.

## Altri ticket aperti

Tutti i ticket aperti non nella top-10 (TICKET-024, TICKET-026, TICKET-066, TICKET-078-deferred, TICKET-081a–116) sono tracciati in [`docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`](docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md) e nella history git pre-Step-6. Le specifiche di accettazione complete sono nei file [`docs/tickets/`](docs/tickets/) individuali.

## Recently closed (atomic `main` commits, giugno–luglio 2026)

| ID | Area | Commit |
|---|---|---|
| M1.5#1 | TextRunNode.cpp orchestratore (Result<OwnedFB,NodeExecutionError>) + 3 helpers in `src/render_graph/nodes/text_run/` (execution / transform / diagnostics) + test_text_run_node_return_channel.cpp locka return channel | Done |
| TICKET-camera-policy-pre-existing | `src/render_graph/pipeline/camera_change_policy.cpp:24` — `Camera2_5D::projection_mode` non esiste più nel current struct; pre-existing rot (`git diff HEAD -- src/render_graph/pipeline/camera_change_policy.cpp` vuoto → NON causato da M1.5#1). Blocca `chronon3d_render_graph_tests` LINK step. Suggested remediation: ripristinare il field o refactor call-site verso `CameraProjection({...})` come già fa `CameraProjectionResolver`. | PLANNED |
| TICKET-077 | gate-3 I2 software_renderer.hpp LOC 223→182 | Done |
| TICKET-079 | gate-3 I5 attach_software_backend SWRenderer& → SWRenderer* (commit `ac5f7125` cleanup phase) | Done |
| TICKET-078 | gate-3 I3 non-local includes 7→6 | Done |
| P1-CMAKE-01 | CMake core/video boundary fix (P1 #5) | Done |
| TICKET-087 | gate-3 I3 build-fix preflight_fonts | Done |
| TICKET-106 | gate-3 I5 path-list-parity regression test | Done |
| TICKET-080 | unicode extraction utf8_decoder + whitespace | Done |
| TICKET-086 | thread_local cache → static + mutex | Done |
| TICKET-088 | HarfBuzz buffer pool in FontEngine | Done |
| TICKET-090 | draw_text_run private stages refactor | Done |
| TICKET-090b | draw_text_run stage-marker comments | Done |
| TICKET-100 | eliminate legacy materialize_text_run_shape | Done |
| TICKET-101 | compile_text_layout (TextDocument, paragraph_index) | Done |
| TICKET-092 | per-paragraph error accumulator | Done |
| TICKET-103a | TextLayoutRequest direction/language/features | Done |
| TICKET-104 | PendingTextRun consumed real-decrement | Done |
| TICKET-105 | identity/preservation regression test suite | Done |
| TICKET-107 | per-category register helpers promoted | Done |
| TICKET-108a | aliases.hpp 7 typedefs adopted | Done |
| TICKET-109 | TextRunBuilderInspector + migrate 53 callsites | Done |
| TICKET-098 | text_preset_registry per-category helpers | Done |
| TICKET-099a | internal-only domain type aliases | Done |
| TICKET-077b | std::call_once elimination (6 files) | Done |
| TICKET-076 | wrap_push.sh auto-FF merge before gate | Done |
| TICKET-075 | GATE-MNT-01 strict-SHA → ancestor-relation | Done |
| TICKET-069 | FreeTypeFaceCache destruction contract | Done |
| TICKET-074 | remove dead glyph_selector_v2 pipeline | Done |
| TICKET-070 | SoftwareBackendServices validation | PARTIAL |
| TICKET-068 | crossfade+stroke regression test | Done |
| TICKET-118 | SoftwareBackend::draw_node real impl + drop dummy TextRunProcessor | Done |
| TICKET-119 | SoftwareBackend m_owner back-pointer removal + ProcessorSourceExtras bridge | Done |
| P0-1 | RenderGraphNode::execute() → Result<OwnedFB, NodeExecutionError> (36 file) | Done |
| P0-2 | FontLayoutIdentity unificata su cache/hash/fastpath/prewarm (5 file) | Done |
| TICKET-PUBLIC-MANIFEST-01 | CMake public-manifest sed-artefact repair (commit `59db2da5`) | Done |
| Gate-10 build rot | consumer SDK + install_consumer_test Phase 1-3 verde (commit `ac5f7125`) | Done |
| Gate-10 Phase 4 (CMake) | consumer-SDK CMake case-fix (TBB/xxHash title-case) + 44 transitive consumer headers to `cmake/Chronon3DPublicHeaders.cmake` (commit `21b9fb5d`) | Fix pushed (CI machine-verified Phase 4 verde still pending) |
| Gate-10 Phase 4 (Runtime) | `RenderPipeline::debug_graph` C++ default-arg chain riparata aggiungendo `float fps = 0.0f` sentinel (commit `75035f2b`, ora `c40ba16f` post-rebase) | Fix pushed (Phase 4 CI end-to-end pending) |
| Gate-10 Phase 4 (Audit-handoff) | Phase 4 verde end-to-end ancora blocker su `efd841f0`; primo transitive hit non risolto = `include/chronon3d/core/memory/detail/framebuffer_impl.hpp` (riga 27 di `core/memory/framebuffer.hpp`); era nella lista audit-v3 missing-15 (3za riga); 14 wrappers glm/tracy/magic_enum ancora pending in TICKET-GATE-10-PHASE-4-FULL | Logged (machine run on `efd841f0` confirmed: install_consumer_test.sh Phase 4 fatal error: `detail/framebuffer_impl.hpp: No such file or directory`) |
| TICKET-render-pipeline-fps-defaults-audit | audit `render_scene` overloads (header lines 71–79) + free-fun `float fps` params in `src/runtime/render_pipeline.cpp:32,54` per assicurare consistenza con la Cat-1 fix `75035f2b` (code-review hygiene nit, non-blocking) | PLANNED |
| TICKET-GATE-10-PHASE-4-FULL | 15 missing non-internal transitive headers (vendored glm/tracy/magic_enum wrappers) — audit `/tmp/audit_v3.py`. **Concrete next-hit machine-verified on `efd841f0`**: `include/chronon3d/core/memory/detail/framebuffer_impl.hpp` (1st of 15). Suggested remediation: incremental batch-add to `cmake/Chronon3DPublicHeaders.cmake` (1 commit per header) or wholesale adoption via `install(DIRECTORY include/chronon3d/ DESTINATION ... PATTERN "internal" EXCLUDE)` ADR per DOCUMENTATION_GOVERNANCE.md. | PLANNED | first candidate: `core/memory/detail/framebuffer_impl.hpp` (Cat-1 install-pipeline) |
| TICKET-067 | GATE-MNT-01 divergence fix | Done |
| TICKET-096 | stash@{0} debt triage (DROP both: alphabetize retro-fixup `eed2cc9b` + helper-script non-idempotency retro-DROP) | Done |

Cronologia completa: [`docs/CHANGELOG.md`](docs/CHANGELOG.md) e [`docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`](docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md).
| Main@efd841f0 gate-audit historical | gate-run snapshot | 10/11 machine-verified on commit `efd841f0` ahead-of-baseline=15; gate #10 `install_consumer_test.sh` FAIL infra-setup with 0s elapsed (carry-over from `bc1c7b9e` `a0e8f14c` text rot forward-fix, code path never exercised) | Done (snapshot) | historical reference | Reproducible with `git worktree add --detach /tmp/gates-efd841f0 efd841f0 && bash <efd841f0>/tools/<gate>.sh`; tee'd per-gate logs in `reports/perf/main-efd841f0-tee/`. |
## Fix-forward ticket references (TICKET-NNN reserved)

| TICKET-PUBLIC-MANIFEST-01 | P0 | CMake public-manifest corruption (sed-leak in commit `28004f96`) | Done | install boundary | fix-forward commit `59db2da5` |
| TICKET-GATE-7-R1 | P1 | `src/runtime/**` non documentato in `CURRENT_STATUS.md` + `ROADMAP.md` | PLANNED | `check_doc_sync.sh` gate #7 | R1 doc-sync repair (post-commits `28004f96`, `59db2da5`, `ac5f7125`) |
| TICKET-GATE-4-LEAK | P1 | `reports/perf/main-73a2aa9b-gates.json` tracked con abs-path leak | PLANNED | `check_gitignored_dirs.sh` gate #4 | Reroute or relocate-replace baseline log (`reports/perf/` non dovrebbe essere tracked) |
| TICKET-GATE-10-PHASE-4 | P1 | consumer-build Phase 4 fallisce su `tbb`/`oneTBB` non disponibile + C++ syntax error in `RenderPipeline::debug_graph` + `kBlurTierRadii` undeclared | ACTION TAKEN (CI verify pending) | `install_consumer_test.sh` gate #10 Phase 4 | triple fix pushed: (a) `21b9fb5d` cmake case-fix + 44 transitive headers; (b) `75035f2b` runtime default-arg chain; (c) TICKET-Phase4-BlurTierRadii compile-time array {{0,2,7,13,20}}. Run Phase 4 end-to-end CI confirmation ancora pending |
| TICKET-Phase4-BlurTierRadii | P1 | `text_run_processor.cpp` references `kBlurTierRadii` (lines 594, 828) but declaration was unintentionally lost in prior upstream refactor of this TU | Fix pushed | `install_consumer_test.sh` gate #10 Phase 4 | restored `static constexpr std::array<i32, 5> kBlurTierRadii = {{0, 2, 7, 13, 20}};` next to existing `kNumBlurTiers` constant. Values transcribed verbatim from the documented tier mapping block at lines ~568-575 of the same TU. |
| TICKET-PUBLIC-MANIFEST-02 | OPP-include-path cascade closure (22 OPP-internal + 1 PUBLIC-manifest + 1 OPP-internal-moved + 1 python-test rewrites) | Done | Cascade-close (2026-07-02). |
| TICKET-render-session-cpp-brace | OPP-side `src/runtime/render_session.cpp` missing namespace-closing `}` (pre-existing OPP-cpp debt) | Done | Pushed together with TICKET-PUBLIC-MANIFEST-02 to ensure OPP compile progresses to unit 162/340. |
| TICKET-PUBLIC-MANIFEST-03 | pre-existing OPP-cpp incomplete-type in `src/backends/software/software_backend.cpp` (pre-existing OPP-cpp debt) | PARTIAL | `[SUPERSEDED BY ac5f7125]` — Minimal fix (a): include `<chronon3d/scene/model/render/render_node.hpp>` + `<chronon3d/backends/software/software_registry.hpp>` from `software_backend.cpp` directly. Closed in commit `ac5f7125`. |

## Cascade-close (TICKET-PUBLIC-MANIFEST-02) + TICKET-PUBLIC-MANIFEST-03 backlog (2026-07-02)

| ID | Area | Status | Notes |
|---|---|---|---|
| TICKET-PUBLIC-MANIFEST-02 | OPP-include-path cascade closure (22 OPP-internal + 1 PUBLIC-manifest + 1 OPP-internal-moved + 1 python-test rewrites) | Done | Pushed in this turn. |
| TICKET-render-session-cpp-brace | OPP-side `src/runtime/render_session.cpp` missing namespace-closing `}` (pre-existing OPP-cpp debt) | Done | Pushed together with TICKET-PUBLIC-MANIFEST-02 to ensure OPP compile progresses to unit 162/340. |

### Open: TICKET-PUBLIC-MANIFEST-03 — pre-existing OPP-cpp incomplete-type in `src/backends/software/software_backend.cpp`

- **Severity**: P1 (blocks OPP compile end-to-end, NOT introduced by commit 28004f96).
- **Affected files**: `src/backends/software/software_backend.cpp` lines 143, 146, 148, 154; forward-declaration boundary at `include/chronon3d/render_graph/render_backend.hpp` line 21 (`struct RenderNode;`) and `include/chronon3d/backends/software/software_processor_context.hpp` line 34 (`class SoftwareRegistry;`).
- **Root cause**: OPP-side `SoftwareBackend::draw_node` accesses `node.shape.type()` (full type required) and `m_proc_ctx.registry->get_shape(...)` (full type required), but the public `RenderBackend` interface only forward-declares these types. OPP-side cpp must either include the full types in their source or be moved behind the `SoftwareProcessorContext` boundary.
- **Suggested remediation**: Either (a) include `<chronon3d/scene/model/render/render_node.hpp>` + `<chronon3d/backends/software/software_registry.hpp>` from `software_backend.cpp` directly (minimal fix), OR (b) restructure so `SoftwareBackend::draw_node`'s reach to full types is mediated through the `m_proc_ctx` bundle (cleaner but larger refactor).
- **Resolution (ac5f7125)**: option (a) applied — `software_backend.cpp` ora include i full types. Close.
- **Tracking**: `tools/check_architecture_boundaries.sh` already passes (gate-3 is forward-decl-only allowed).

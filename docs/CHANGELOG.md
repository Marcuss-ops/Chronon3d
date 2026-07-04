# Chronon3D — Changelog

> Lavoro completato su `main`. Per i dettagli completi di ogni ticket: [`docs/tickets/`](docs/tickets/).
> Per lo stato corrente: [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md).

---

## Luglio 2026 — Chiusure recenti

### camera — Camera V1 trajectory preserves lens/DOF/motion_blur/parent + OrientAlongPath real tangent/roll + keep_horizon toggle + degenerate-tangent safety
- `src/scene/camera/camera_v1/camera_program.cpp` trajectory branch (`evaluate_compiled_source`) ora preserva **l'intera** `CameraBaseSpec` (lens, DOF, motion blur, parent_name) invece di hardcodare `zoom=1000, fov=50` + drop dei campi residui. Il route canonico è `apply_projection_spec(...)` + carry-forward di `tangent` + `roll_deg` per `OrientAlongPath`.
- `tests/scene/camera/test_camera_program_compiled.cpp` §1.B: 5 nuove TEST_CASEs lockano il contratto (compiled_trajectory_*: projection_variant, lens, dof/motion_blur/parent, roll_deg). §4.B TICKET-022 single-application canonical-order lock (determinism + canonical rotation + skip-look-at-constraint). §4.C TICKET-024 orbit position in camera-local basis (yaw 0/90/180/270 + track.x camera-local flip + dolly camera-local forward + rotation coherence). §2.A TICKET-021 variance-preserving dispatch per `PoseTracksSource` (FOV/PhysicalLens/AnimatedFOV).
- `tests/scene/camera/test_orient_along_path.cpp` (Step 4, nuovo): 4 TEST_CASEs lockano il contratto semantic di `OrientAlongPath` (tangent valid + keep_horizon, tangent valid + keep_horizon=false + roll_deg, degenerate + last_tangent preservation, keep_horizon override base rotation). Companion a §1.B.
- `tests/scene/camera/test_camera_program_compiled.cpp` §10 GOLDEN regression (Step 5, nuovo): `compiled_trajectory_lens_dof_golden` con FNV-1a hash su `camera.position + camera.lens + camera.dof + camera.rotation` + desc.id per 5 eval consecutive (determinism lock). Pinned snapshot in `tests/scene/camera/_golden/trajectory_lens_dof.golden.bin` (placeholder sentinel 0xDEADBEEFDEADBEEF). Regen script `tools/regen_camera_golden.sh` (esplicito, non in CI): upstream-blocker preflight probes (`camera_program_compiler.cpp:330-335` size→points().size() + `golden_projection_test.cpp: FocalPx` linker) → cp-fallback defensive snapshot → rm-f to force sentinel-detection on every regen → python3 availability gate → hash MESSAGE() capture → 8-byte LE .bin write → git add with rev-parse worktree pre-check.
- AGENTS.md v0.1 freeze Cat-3 (test deterministici — golden test). Zero nuovi simboli pubblici; tutti i lock vivono dentro `src/scene/camera/camera_v1/*` (carry-forward), `tests/scene/camera/test_camera_program_compiled.cpp` (§1.B, §2.A, §4.B, §4.C, §10), `tests/scene/camera/test_orient_along_path.cpp` (Step 4), e lo script regen.
- Companion tickets chiusi: TICKET-021 (variance-preserving dispatch), TICKET-022 (single-application canonical-order lock), TICKET-024 (orbit position camera-local basis), TICKET-025 (OrientAlongPath semantic correctness). Stato promosso da `PARTIAL`/`PLANNED` → `DONE` (vedi `docs/FOLLOWUP_TICKETS.md` Recently closed + `docs/CURRENT_STATUS.md` §Camera V1 DoD sub-section). Note: il full `chronon3d_scene_tests` target è attualmente blocked da 2 pre-esistenti rot su `main` (`golden_projection_test.cpp` 4× `FocalPx` undefined + `camera_program_compiler.cpp:330-335` `size()` vs `points().size()`) — fuori scope del cluster Camera V1; i singoli TU sono compile-clean in isolation.

### build — Cat-1 build rot clear scene_tests link (commit `7ee302bf`)
- 3 Agent3-introduced test-compile regressions chiuse in atomic commit `7ee302bf`:
  - `tests/scene/camera/golden_projection_test.cpp`: 4× `FocalPx` → `camera_math::FocalPx` (matches canonical usage in `camera_2_5d_projection.hpp:199`, `camera_projection_resolver.hpp:134/351/518/533`, `evaluated_projection.hpp:124`). Header already includes `camera_projection_contract.hpp`; fix was namespace qualification only.
  - `tests/scene/camera/test_camera_context_framerate_propagation.cpp`: 2× `fz.fps.num`/`fz.fps.den` → `fz.fps.numerator`/`fz.fps.denominator` (`chronon3d::FrameRate` declared at `core/types/time.hpp:9-26` exposes DATA FIELDS `.numerator`/`.denominator`, not `.num()`/`.den()` — the latter are accessor methods, not fields). Added explicit `#include <chronon3d/core/types/time.hpp>` per IWYU (was previously reachable transitively via `sample_time.hpp`).
  - `tests/scene/camera/test_camera_lookat_layer_missing_transforms.cpp`: added `#include <chronon3d/scene/camera/camera_v1/camera_session.hpp>` (`camera_program.hpp:36` only forward-declares `struct CameraSession;`; the test constructs `CameraSession session{}` on L118/L150/L169/L190 which requires the complete type).
- Verification: in-isolated TU compile GC=0 across all 3 fixed TUs; full `chronon3d_scene_tests` link GC=0 (cleared from 3 distinct blockers to 1 remaining pre-existing rot — the `size()`/`points().size()` mismatch in `camera_program_compiler.cpp:330-335`).
- AGENTS.md v0.1 freeze Cat-1 (build corrections — test-side scope). Zero nuove API pubbliche; tutti i fix vivono dentro test-side TUs + una dichiarazione-namespace-qualifier; nessun header `include/chronon3d/` modificato.

### docs(status) — Camera Production V1 row recovery (commit `c5793405`)
- `docs/CURRENT_STATUS.md` "Stato generale per area" table had the `Camera Production V1` row accidentally deleted by a faulty sed regex during the `7ee302bf` rebase-conflict resolution:
  ```sh
  sed -i '/^| Camera Production V1.*Projection contract closed (C1/d' docs/CURRENT_STATUS.md
  ```
  The regex matched BOTH the HEAD-side row AND the REMOTE-side row because both contained the substring `"Projection contract closed (C1..."`; both rows were dropped, leaving the table without the Camera entry.
- Follow-up commit `c5793405` re-inserts the row between the `Text Production V1` and `SDK C++ installabile` rows with updated post-Cat-1-fix factual state:
  - Agent3 C1–C7 projection contract (`FocalPx`/`ViewportRect`/`focal_xy_from_camera`/`effective_viewport` con offset; golden test 6-mode committed `tests/scene/camera/golden_projection_test.cpp`).
  - Agent1 Step 4+5 trajectory work (lens/DOF/motion_blur/parent carry-forward + OrientAlongPath real tangent/roll + keep_horizon + degenerate-tangent safety + §10 GOLDEN regression + TICKET-021/022/024/025 DONE).
  - 6/6 CAM-DOC 04 arch-boundary DoD PASS (`tools/check_camera_architecture.sh`).
  - Cat-1 build-rot commit `7ee302bf` cleared FocalPx / FrameRate / CameraSession test-compile regressions; full `chronon3d_scene_tests` link now GREEN.
  - 1 pre-existing on-`main` rot rimane aperto: `size()` vs `points().size()` in `camera_program_compiler.cpp:330-335` — TICKET independent, out of scope Cat-1 step.
  - Runtime certification + framing/clipping/DOF + legacy migration ancora aperti.
- `tools/check_doc_sync.sh` PASS post-recovery (gate #7 AGENTS.md); only the canonical `docs/CURRENT_STATUS.md` was touched.
- AGENTS.md v0.1 freeze Cat-1 (documentation-only correction; no source/test/tools changes).

### audit — close `TICKET-render-pipeline-fps-defaults-audit` (Policy E; no header change; fps uniformly no-default)
Audit on `float fps` parameters across `RenderPipeline::render_scene` overloads + `RenderPipeline::debug_graph`: aired originally as code-review nit on commit `fc144fa2`.  **Verdict: no code change** — all scanned methods on `RenderPipeline` (header), the `RenderPipeline::render_scene` member-fn bodies in `src/runtime/render_pipeline.cpp:32,54` (matching header signatures), the lower-level free functions in `include/chronon3d/render_graph/pipeline/render_pipeline.hpp` (`chronon3d::graph::render_scene_via_graph` + `debug_scene_graph`), and `SoftwareRenderer::render_scene` definitions in `include/chronon3d/backends/software/software_renderer.hpp` (+ `.cpp`) ALL require the caller to pass `float fps` explicitly (no default).  This preserves upstream `6df9b429` ("P1 #10 - remove hardcoded 30.0f fps defaults from core pipeline") intent exactly.  The `= 0.0f` sentinel on `debug_graph`'s `frame_time` parameter is unrelated to `fps` and exists strictly to satisfy the C++ default-argument contiguity rule around `Frame frame = 0`.  AGENTS.md v0.1 freeze Cat-1 (build corrections — install-pipeline plumbing).  Zero new public symbols; pure audit closure (No-Ops commit body).  Companion spec: [`docs/tickets/TICKET-095.md`](tickets/TICKET-095.md).  Origin: code-reviewer-minimax-m3 nit on commit `fc144fa2` (3-line comment-trim retro-fixup to `75035f2b`'s default-arg chain fix); non-blocking.

### hygiene — drop non-idempotent manifest helper script (retro-fixup to eed2cc9b)
- `tools/c3d_manifest_alphabetize.py` (added previously on `eed2cc9b`):
  dropped because it crashes on already-alphabetized manifests
  (`AssertionError: parse fail: bulk=None`) — non-idempotent on the
  committed state.  The retro-fixup claim "re-running yields zero diff"
  was a degenerate true (crash-before-write).  Replacing with a
  CI-enforced invariant rather than a worker script: future alphabetize
  drift will surface via the existing `tools/check_public_headers.py`
  harness, not via a brittle utility.
- AGENTS.md v0.1 freeze Cat-1 (build corrections — install-pipeline
  plumbing).  Zero new public API symbols; pure hygiene.

### hygiene — alphabetize Chronon3DPublicHeaders manifest (retro-fixup to 21b9fb5d)
- `cmake/Chronon3DPublicHeaders.cmake`: alphabetized the 43 bulk-appended
  TICKET-GATE-10-PHASE-4 entries into the canonical bucket ordering
  (`animation/api/assets/cache/compositor/core/effects/graphics/layout/math/media/...`),
  interleaving them inline with the existing 105 transitive-closure entries
  rather than appending under a single bulk block.  `internal/` prefix is
  stripped per module-bucket sort so `internal/render_graph/` sorts under
  `render_graph/`, preserving the established module-level bucket pattern.
- Resolved and stripped the temporary bulk-append `TICKET-GATE-10-PHASE-4`
  comment block (the bulk contract was retired by the inline reset).
- Deduplicated `core/memory/detail/framebuffer_impl.hpp` (upstream
  regression introduced during the rebase), enforcing the
  "every header enumerated explicitly once" invariant.  Added the
  previously-missing canonical `core/memory/framebuffer_handle.hpp` +
  `core/memory/framebuffer_slot_view.hpp` entries (file-on-disk existence
  verified pre-commit).
- Tracked the `tools/c3d_manifest_alphabetize.py` helper for maintainer
  idempotency (`tools/` is git-tracked per AGENTS.md and other gates live
  there).  Future alphabetize passes can replay it via
  `python3 tools/c3d_manifest_alphabetize.py` to verify zero diff.
- AGENTS.md v0.1 freeze Cat-1 (build corrections — install-pipeline
  plumbing).  Zero new public API symbols; strictly internal CMake
  manifest sorting and audit-trail header preservation.
- Applied retroactively to commit `21b9fb5d` per code-reviewer-minimax-m3
  nudge (parallel format to the `render_pipeline.hpp` retro-fixup commit
  `fc144fa2`).

### hygiene — trim TICKET-GATE-10-PHASE-4 comment block in `render_pipeline.hpp` (retro-fixup to 75035f2b)
- `include/chronon3d/runtime/render_pipeline.hpp:90` above the
  [[nodiscard]] `std::string debug_graph(...)` declaration had a 7-line
  TICKET-GATE-10-PHASE-4 fix-up comment; collapsed to 3 lines that keep
  every actionable breadcrumb (upstream commit `6df9b429`, the C++
  default-argument contiguity rule, the Cat-1 sentinel `= 0.0f`, and the
  "no hardcoded fps literal" intent).  Applied retroactively to commit
  `75035f2b` per code-reviewer-minimax-m3 nudge.  Zero new public
  symbols; pure-comment reduction.

### text-run — `kBlurTierRadii` compile-time array restoration (commit TICKET-Phase4-BlurTierRadii)
- `src/backends/software/processors/text_run/text_run_processor.cpp`: aggiunto
  `static constexpr std::array<i32, kNumBlurTiers> kBlurTierRadii = {{0, 2, 7, 13, 20}};`
  accanto al constant già presente `static constexpr int kNumBlurTiers = 5;`
  (line 577). Valori trascritti verbatim dal documented tier-mapping block
  già esistente alle righe 568–575. Root cause: l'array era riferito dalla
  lambda `bucket_radius()` (line 594) e dal render dispatch per-tier (line 828),
  ma la dichiarazione era andata persa in un precedente upstream refactor di
  questo TU (probabilmente una `git mv`-style move che ha dimenticato di
  portarsi dietro la definizione).
- AGENTS.md v0.1 freeze Cat-1 (build corrections). Zero nuove API pubbliche;
  valori letterali preservano l'algoritmo di blur documentato (radius mapping
  tier 0→0, tier 1→2, tier 2→7, tier 3→13, tier 4→20 (capped)).
- Verification: Phase 4 end-to-end verde ancora da certificare in CI
  (prossima run `bash tools/install_consumer_test.sh`).
- Followup pendente: TICKET-Phase4-BlurTierRadii-audit (analogo a
  TICKET-render-pipeline-fps-defaults-audit) per scan di altri constexpr
  array riferiti ma non dichiarati in questo TU.

### runtime — `RenderPipeline::debug_graph` default-arg chain fix (commit `75035f2b`, post-rebase `c40ba16f`)
- `include/chronon3d/runtime/render_pipeline.hpp:90`: aggiunto `= 0.0f` sentinel
  al parametro `float fps` di `debug_graph(...)`.  Root cause: upstream commit
  `6df9b429` "fix(render): P1 #10 - remove hardcoded 30.0f fps defaults" aveva
  rimosso il default dal parametro 7 ma lasciato i default sui parametri 5/6
  (`Frame frame = 0`, `f32 frame_time = 0.0f`), violando la regola C++ di
  default-argument contiguity. C++ compile error in
  `tools/install_consumer_test.sh` Phase 4 (consumer build esterno).
- AGENTS.md v0.1 freeze Cat-1 (build corrections). Zero nuovi simboli pubblici;
  il sentinel `0.0f` preserva l'intento upstream di nessun hardcoded fps literal.
- Verification: Phase 4 end-to-end verde ancora da certificare in CI.
- Followup aperto: `TICKET-render-pipeline-fps-defaults-audit` per gli altri
  `float fps` parametri (header lines 71–79 + free-funs in
  `src/runtime/render_pipeline.cpp:32,54`) — code-review nit, non-blocking.

### cmake/SDK — TICKET-GATE-10-PHASE-4 case-fix + transitive consumer headers (commit `21b9fb5d`)
- `cmake/Chronon3DRegistry.cmake`: case-fix in `CHRONON3D_SDK_PUBLIC_DEPS` —
  `"TBB::tbb|tbb"` → `"TBB::tbb|TBB"`,
  `"xxHash::xxhash|xxhash"` → `"xxHash::xxhash|xxHash"`. Necessario perché il blocco
  `find_dependency(...)` auto-generato nell'installed `Chronon3DConfig.cmake`
  emetteva lookup lowercase non risolvibili su Linux ext4 (case-sensitive FS)
  contro `vcpkg_installed/x64-linux/share/tbb/TBBConfig.cmake` /
  `xxHashConfig.cmake` (TitleCase). vcpkg snapshot 2026-05-27-d5b6777d.
- `cmake/Chronon3DPublicHeaders.cmake`: 44 install-pipeline-only entries —
  1 inline `core/dirty_fallback_reason.hpp` (transitivo in `core/profiling/counters.hpp`)
  + 43 transitive-needed mass-appended sotto blocco comment `TICKET-GATE-10-PHASE-4`.
  Audit invariants (replay via `/tmp/audit_v3.py`): manifest 149 → 193, missing non-internal 174 → 15.
- AGENTS.md v0.1 freeze Cat-1 (build corrections — install-pipeline plumbing).
  Zero nuove API pubbliche; nessun `#include` install-time oltre `cmake/`.
- Audit verificato: 16/16 check pass al gate-1 (`tools/check_architecture_boundaries.sh`).
  Phase 4 end-to-end verde ancora da certificare in CI.
- Followup: `TICKET-GATE-10-PHASE-4-FULL` (15 vendored wrappers glm/tracy/magic_enum ancora
  transitivamente richiesti; nuova triage post-this-commit).

### Gate-10 consumer-SDK build-rot fix (commit `ac5f7125`)
- `src/backends/software/software_backend.cpp`: aggiunti include mancanti per `RenderNode` + `SoftwareRegistry` (invalid use of incomplete type nel dispatch `get_shape()->draw`).
- `cmake/Chronon3DPublicHeaders.cmake`: pulizia (era corrotto da artefatti sed nel fix-forward `59db2da5`).
- `src/runtime/render_session.cpp`: aggiunta `} // namespace chronon3d` mancante (residuo dopo il relocation commit `28004f96`).
- `src/backends/software/runtime_adapter.cpp`: `renderer->software_registry()` e `font_engine()` restituiscono ref → aggiunto `&` per `ProcessorSourceExtras` (pointer fields).
- 4 symlink pointer in `include/chronon3d/{runtime,render_graph/cache,render_graph/core}/*.hpp` → file spostati in `include/chronon3d/internal/` dal commit `28004f96` ma caller non aggiornati; i symlink preservano la source-compat ABI mentre si migra gradualmente.
- Risultato gate-10 su `main@ac5f7125`: Phase 1.1–3 OK (SDK build + install nel consumer prefix + canary gate verde, 329 .o files nell'archive). Phase 4 (consumer cmake build esterno) ancora FAIL su `tbb` (vedi `TICKET-GATE-10-PHASE-4`).

### TICKET-PUBLIC-MANIFEST-01 — CMake public-manifest sed-artefact repair (commit `59db2da5`)
- `cmake/Chronon3DPublicHeaders.cmake` era corrotto da artefatti sed `/d\n` iniettati da un heredoc imperfetto durante il commit `28004f96` (sdk-public-surface reduction). CMake-configure falliva con errori `target_sources`.
- Manifest rebuildato prendendo il contenuto pristine da `git show HEAD~1` (commit `28004f96` prima del bug) con i 4 OPP-relocated entries rimossi (`render_session.hpp`, `session_services.hpp`, `scene_program_store.hpp`, `scene_hasher.hpp` → `internal/`).
- Aggiunto comment block `INTERNAL` esplicito che documenta la resolution topology.

### P0 #1 — `RenderGraphNode::execute()` → `Result<OwnedFB, NodeExecutionError>`
- Cambiato il return type di `RenderGraphNode::execute()` da `OwnedFB` a `NodeExecResult` (`Result<OwnedFB, NodeExecutionError>`)
- Aggiunto `Result<T,E>` template in `render_backend.hpp` con `take_value()` per move-only types
- 18+ tipi di nodo aggiornati (headers + .cpp) a restituire `NodeExecResult{...}`
- `run_node()` in `node_runner.cpp`: unwrappa `Result`, su errore scrive a `ctx.frame_error`
- `execute_internal()` in `executor.cpp`: controlla `frame_error` dopo i nodi → restituisce `nullptr`
- Invariante: backend error → node restituisce `NodeExecutionError` → run_node scrive frame_error → executor nullptr → sink non pubblica
- 36 file modificati, 4 test file aggiornati (mock `execute()` → `return NodeExecResult{}`)

### P0 #2 — `FontLayoutIdentity` unificata su cache/hash/fastpath/prewarm
- Nuovo `FontLayoutIdentity` struct in `font_engine.hpp` (font_path, font_family, font_weight, font_style, font_size, features)
- `font_family` aggiunto a `layout_hash()`, `shaping_hash()`, `TextLayoutCacheKey::digest()`, `build_cache_key()`
- Fast-path in `apply_active_state_to_text_run_shape()` ora confronta `FontLayoutIdentity` invece del solo `source_text`
- Font overrides non più gated da `font_path.empty()` (×2 in `text_run_driver.cpp`)
- 5 file modificati: `font_engine.hpp`, `text_run.hpp`, `text_run.cpp`, `text_run_builder.cpp`, `text_run_driver.cpp`

### TICKET-118 — `SoftwareBackend::draw_node` reale + drop dummy `TextRunProcessor`
- `SoftwareBackend::draw_node` non è più un no-op `// Intentionally empty`:
  ora dispatcha `m_proc_ctx.registry->get_shape(shape.type())->draw(...)`,
  con early-return silent sul caso `ShapeType::TextRun` (canonico via
  `multi_source_node` → `draw_text_run`).
- Fallback loud-fail: `m_proc_ctx` mai attachato → `spdlog::error` con
  shape type numberato, così regressioni future su `attach_processor_context`
  appaiono in CI invece di "completarsi" silenziosamente.
- Droppato dummy `TextRunProcessor` in `text_run_processor.cpp` (no-op
  draw + bbox `{0,0,0,0}` + hit_test false). Era un registry marker
  inutilizzato: il dispatch canonico passa da `TextRunNode` →
  `SoftwareBackend::draw_text_run` direttamente.
- Droppata `create_text_run_processor()` (factory + forward-decl + site
  di registrazione in `builtin_processors.cpp::register_builtin_processors`).
- Niente nuova API pubblica; niente nuovo `target_link_libraries`.

### TICKET-119 — `SoftwareBackend` m_owner back-pointer removal + internal bridge
- `SoftwareBackendServices::owner` rimosso (era il `SoftwareRenderer*`
  back-pointer usato da `draw_text_run` per sourcire la
  `SoftwareProcessorContext`).  `MissingOwner` Code rimosso; i restanti
  5 Code mantenuti ma renumbered (MissingCounters=1 →
  MissingTextResources=5).
- `SoftwareBackend` ora owner-free lato software: `m_proc_ctx`
  value-member popolato post-construction via NUOVO metodo pubblico
  `attach_processor_context(SoftwareProcessorContext)`.
- Nuovo header INTERNO `src/backends/software/internal/software_processor_services.hpp`
  (mai installato in `include/chronon3d/`): definisce `ProcessorSourceExtras`
  (registry + image_backend + font_engine) e la free function
  `make_processor_context(services, extras)`. Questo è l'unico path che
  conosce come costruire un `SoftwareProcessorContext` completo da un
  public `SoftwareBackendServices` + i campi orchestrator-only.
- `runtime_adapter.cpp::attach_software_backend`, `tests/helpers/test_utils.hpp`
  ed il file di test di factory (`test_software_backend_factory.cpp`)
  aggiornati per il nuovo wiring.  Per Option A (DELETE-only) thinker-validated:
  il test file ha rimosso i check static-grep / NDEBUG su `MissingOwner` e
  ha aggiunto un nuovo TEST_CASE static-grep che verifica l'applicazione
  della contractive removal (linee TICKET-118 presenti, MissingOwner assente).
- Public-API surface delta: **1 new public method** added to
  `chronon3d::SoftwareBackend` (`attach_processor_context(...)`); the
  underlying `chronon3d::SoftwareProcessorContext` type was already
  public.  No new public classes, no new public headers, no new public
  fields on `SoftwareBackendServices`.
- ABIs invariant: `ProcessorSourceExtras::font_engine` gadget-field remains
  `#ifdef CHRONON3D_HAS_BACKEND_TEXT`-gated like
  `chronon3d::SoftwareProcessorContext::font_engine` (commented in the
  new header); the parent CMakeLists sets the macro uniformly on the
  `chronon3d_backend_software` target so all objects see one layout.

### TICKET-101 — compile_text_layout accetta (TextDocument, paragraph_index)
- Aggiunto `paragraph_index` a `TextLayoutRequest` (POD extension, zero nuove classi pubbliche)
- `compile_text_layout()` indicizza direttamente nel document tree, preserva rich-text 1:1
- `build_text_run()` rifiuta paragrafi multi-font con pre-check
- 3 nuovi TEST_CASEs deterministici

### TICKET-092 — per-paragraph error accumulator
- Introdotto `CompiledParagraphResult` e `TextDocumentCompileResult` (struct interne, non pubbliche)
- `compile_text_document()` accumula Ok AND Err per-paragraph, rimuove skip-on-failure
- `source_index` bridge per `apply_spacing_collapse()`, firma pubblica invariata
- 3 nuovi TEST_CASEs deterministici

### TICKET-105 — identity/preservation regression test suite
- 3 TEST_CASEs deterministici: identity across frames, joint-include, double-include safety
- Stesso `shared_ptr<TextRunLayout>` + `layout_hash()` tra frame N, N+1, N+2
- ODR conflict canonico-vs-narrow documentato, deferred a TICKET-083

### TICKET-103a — TextLayoutRequest direction/language/features
- 3 nuovi campi POD su `TextLayoutRequest`: direction, language, features
- `Bcp47LanguageTag` e `TextShapingFeatures` come type aliases pubblici
- Cache key estesa a 6 parametri, discrimina LTR vs RTL, en vs ar
- 2 nuovi TEST_CASEs di cache collision

### TICKET-104 — PendingTextRun::consumed real-decrement
- `consumed` ora decremento reale con contatore atomico (non più no-op)
- `commit()` valida catena selector/animator, droppa orphan + spdlog::warn
- `mutable_pending()` resta pubblico, doc-comment flagga il bypass
- 2 nuovi TEST_CASEs deterministici + CR5 follow-up CapturingWarnSink

### TICKET-100 — Elimina legacy materialize_text_run_shape pipeline
- 5 fasi legacy consolidate in `compile_text_layout()` canonico
- Cache key legacy preservata bit-identical
- `text_layout->font = primary_font` (chiude review P0 #6)
- 4 nuovi TEST_CASEs identity

---

<!-- Le voci datate `## Maggio–Giugno 2026 — Performance e feature` e tutto ciò
che le precede (incluso la nota `## Fix-forward — corrupted public-header manifest`
sul commit `28004f96`, refactoring di Giugno 2026, pulizie e consolidamenti,
expression system v2 lifecycle) vivono in
[`docs/ARCHIVE/CHANGELOG_2026_H1.md`](ARCHIVE/CHANGELOG_2026_H1.md).

Trimming del CHANGELOG canonical da 340 → ~232 linee applicato 2026-07-02
per rientrare nel limite raccomandato di 150 righe (vedi
[`docs/DOCUMENTATION_GOVERNANCE.md`](DOCUMENTATION_GOVERNANCE.md) §10 — DoD
documentale). Per la regola di governance su cosa vive in `docs/ARCHIVE/`,
vedi la stessa DOCUMENTATION_GOVERNANCE.md §"Documenti di supporto"
(`Materiale storico (non operativo) → docs/ARCHIVE/`). Code-reviewer note #5
applicata. -->

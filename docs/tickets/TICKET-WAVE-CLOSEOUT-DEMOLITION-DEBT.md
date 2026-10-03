# TICKET — Wave closeout demolition debt

This ticket records the compatibility boundaries retained while the typed
compiler/runtime and final video-plan contracts replace older entry points.

## Legacy composition adapter

- **Owner:** runtime/compiler migration.
- **Reason:** `RenderEngine::prepare(Composition)` accepts an arbitrary
  per-frame `Composition::SceneFunction`, while the typed compiler currently
  lowers `RenderPlan` into `CompiledProgram`; no one-frame projection is
  semantically equivalent.
- **Exit condition:** prepared single-frame and batch execution consume only
  `CompiledProgram` plus `FrameParameterBlock`; no production
  `compatibility_composition` or arbitrary callback route remains.
- **Equivalence test/gate:** compiled-program corpus equivalence covering
  frame-varying callbacks, camera evaluation, assets, and batch ordering.
- **Removal scope:** `compatibility_composition`, the Composition preparation
  overload/adapter, compatibility render methods, and their timeline includes.
- **Status:** `ACTIVE`

### FASE C20-C25 — census 2026-09-09 (typed Composition lowering)

- **C20 typed lowering `Composition::SceneFunction → CompiledProgram` — ACTIVE:**
  `RenderEngine::prepare(Composition)` + `PreparedRenderJob::Impl::compatibility_composition`
  (9 refs: `render_engine.cpp:51,66-68`, `render_engine_prepared_batch_detail.hpp:10,21-22`,
  `render_engine_prepared_job_core_detail.hpp:31,36-37`, `render_engine_api_detail.hpp:174`)
  + `RenderPipeline::render_compatibility / render_compatibility_frames`
  (4 refs) rimangono l'unico ponte. `Composition::SceneFunction` (`.hpp:34`)
  è un `std::function<Scene(const FrameContext&)>` per-frame — nessuna proiezione 1-frame è semanticamente equivalente;
  la lowering deve produrre timeline/parameters tipizzati → `CompiledProgram`, non snapshot frame 0.
- **C21 guard frame-0 — FATTO (principle respected):** grep su `src/timeline/` + `include/chronon3d/timeline/` trova zero
  conversioni che campionano `Composition` al frame 0; `compile_evaluate.cpp` valida `SceneFunction` presence e
  valuta per-frame via `FrameContext`. Il ponte compatibility resta finché una lowering tipizzata non esiste — mai `Composition → CompiledProgram` via frame 0.
- **C22 equivalence corpus — APERTO (plan locked):** corpus da coprire prima di cancellare `compatibility_composition`:
  `animated transform`, `opacity`, `text changes`, `camera changes`, `dynamic layer visibility`,
  `frame-varying callbacks rappresentabili`, `assets`, `batch ordering`. Gate: render deterministici frame-by-frame
  `compatibility_composition → render_compatibility*` vs `CompiledProgram → RenderPipeline::render*` con hash identici.
- **C23 delete compatibility — APERTO (blocked on C22 green):** removal scope bloccato: `compatibility_composition`,
  `render_compatibility()`, `render_compatibility_frames()`, Composition preparation adapter, `CompiledComposition` runtime path,
  timeline compatibility includes. Da eseguire solo a `C22 PASS`.
- **C24 non ottimizzare mutex/CV compatibility pipeline — FATTO:** `RenderPipeline::render_compatibility_frames` mantiene
  `std::mutex wait_mutex` + `std::condition_variable stage_changed` + 3 thread (`evaluation_thread/render_thread/encoder_thread`)
  con `notify_all()`. Intenzionalmente non ottimizzato: il path è demolition target, polishing SPSC sarebbe spreco.
- **C25 BoundedSpscRing after demolition — NOT YET (deferred):** `BoundedSpscRing` già canonico (`include/chronon3d/runtime/bounded_spsc_ring.hpp`);
  se dopo la demolition resta una pipeline `eval→render→encode`, riusare ESCLUSIVAMENTE quella queue — nessuna seconda queue implementation. Non toccare finché C23 non è REMOVED.

- **Status:** `ACTIVE`

## Legacy video resolver helper

- **Owner:** media/video execution migration.
- **Reason:** the resolver migration retained an internal helper while callers
  moved from path flags to `ExecutionRequirements`, `OutputSpec`, and
  `VideoCapabilities`.
- **Exit condition:** all resolver decisions are produced directly from the
  semantic input contract and no compatibility-named helper or duplicate path
  view remains.
- **Equivalence test/gate:** `tests/cli/test_packet_assembler_contract.cpp`
  resolver matrix, including direct-YUV, full-graph, packet-copy, and invalid
  capability outcomes.
- **Removal scope:** the old helper and its legacy flag translation; keep the
  single public `resolve_video_execution*` authority and `VideoExecutionPlan`.
- **Status:** `REMOVED` — helper renamed to `resolve_plan_from_capabilities`;
  no `resolve_legacy_internal` symbol remains; the final GOP equivalence matrix
  passes in `tests/cli/test_packet_assembler_contract.cpp`.

## Software renderer execution-context migration

- **Owner:** software backend/runtime boundary.
- **Reason:** `SoftwareRenderer` historically exposed a `RuntimeContext&`
  back-pointer to every dispatch TU. This made the renderer a service locator
  and let hot paths re-resolve caches, backend, resolver and scheduler.
- **Landed boundary:** `SoftwareExecutionContext` groups the resolved registry,
  scratch, image, text and settings views; `RenderPipeline` and graph/effect
  dispatch consume that bundle. It is constructed once during backend
  attachment and is rebuilt only after renderer move operations.
- **Runtime accessor:** **REMOVED** — `SoftwareRenderer` has no `runtime()`
  member in the public header, and a source census finds no code callers. The
  renderer class remains the active software backend and is not a demolition
  target.
- **Remaining exit work:** remove the remaining runtime-header coupling from
  the software-target public surface. Do not describe this as legacy runtime
  callers; the outstanding debt is compile-time boundary cleanup.
- **Acceptance:** processor construction and render-pipeline dispatch no
  longer need a runtime lookup; final exit remains zero software-backend
  `runtime()` calls and a software-target compile without runtime-header
  coupling.
- **Status:** `PARTIAL — accessor/callers removed; header boundary remains`

# Compiler Demolition Waves — Wave 1–4

Status: `CLOSED` (re-verified against `2c921aa3b` on 2026-10-10; Waves
1/2/3/4 execution demolition complete — compat shims remain only as
public-API adapters, not executor payload)

Obiettivo: lasciare una sola authority per execution, job normalization,
compiler output e graph lowering, senza introdurre una seconda pipeline
temporanea. Ogni riga diventa `DONE` solo quando il codice, i caller e il gate
sono coerenti.

## Todo / acceptance criteria

- [x] Wave 1 — `command_plan_executor` rimosso da production e test helper.
- [x] Wave 1 — `CommandPlan` resta solo come IR/contratto dati e passa da
  `GraphExecutor`.
- [x] Wave 1 — authority gate senza `execute_command_plan` o
  `command_plan_executor` in `src/`, `include/` e `apps/`.
- [x] Wave 2 — `RenderJobRequest` e `normalize_render_job()` sono l’unica
  normalizzazione backend/codec/range per il percorso render-plan.
- [x] Wave 2 — CLI e IPC invocano lo stesso entrypoint request-based, senza la
  lista legacy di parametri e senza il passaggio IPC policy → overrides.
- [x] Wave 2 — `RenderPlanState` resta solo DTO CLI; la policy non è più
  risolta da questo adapter e il job executor riceve il risultato normalizzato.
- [x] Wave 2 — il percorso render-plan trasporta un solo `CompiledProgram`;
  la `CompiledComposition` legacy viene materializzata una sola volta nel
  resolver per gli executor ancora non migrati.
- [x] Wave 3 — `CompiledProgram` immutable con topology, parameters e
  fingerprint esposti dalla compiler boundary.
- [x] Wave 3 — `ProgramMetadata` owns the output contract, prepared-asset
  manifest, request/content digests and admission budget; its public header
  no longer includes a `render_plan/*` header.
- [x] Wave 3 — il render-plan runtime esegue direttamente topology e parameter
  programs di `CompiledProgram`, senza materializzare `PreparedRenderPlan` /
  `CompiledComposition` come payload dell’executor (verified 2026-10-10
  against `2c921aa3b`: `RenderPipeline::render()` richiede `CompiledProgram`,
  campiona via `TypedProgramSampler::sample_into` + `SceneMaterializer`,
  cache per `fingerprint()`; nessun `PreparedRenderPlan`/`CompiledComposition`
  in `job_session`/`frame_execution`; `typed_program_sampler` zero riferimenti
  al bridge; `render_compiled_composition` resta solo adapter compat per
  prepared-job legacy e SDK file methods).
- [x] Wave 3.1 — il payload dello slot non possiede più
  `EvaluatedCompositionFrame`: scene e camera frame-local sono possedute dal
  lease come stato tipizzato separato.
- [x] Wave 3.2 — il graph cache usa il `ProgramFingerprint` quando il percorso
  typed lo fornisce; i caller authoring mantengono il fallback `SceneHasher`.
- [x] Wave 3.3a — `SceneProgramStore` è esposto al precomp executor tramite
  un puntatore esplicito di `RenderGraphContext`, preparando la rimozione del
  service accessor da `JobSession`.
- [x] Wave 3.3b — i test precomp possiedono il `SceneProgramStore` come
  dipendenza esplicita del contesto, senza leggerlo dal `JobSession`.
- [x] Wave 3.3c — il reset frame/job non invalida più il program cache store;
  la sua gestione è separata dal ciclo di vita dei valori frame.
- [x] Wave 3.3d — il percorso software possiede `SceneHasher` e
  `SceneProgramStore` in `SoftwareSessionResources`; il contesto di grafo
  riceve il puntatore esplicito dal renderer e il job non è più consultato
  dall’executor.
- [x] Wave 5.1 — il compiled executor accede al backend tramite il seam
  tipizzato `RenderGraphContext::execution_backend()` invece del bundle
  `RenderServices`.
- [x] Wave 5.2 — il lifecycle executor dei framebuffer usa il seam tipizzato
  `execution_framebuffer_pool()`.
- [x] Wave 5.3 — il node-cache evaluator usa il seam tipizzato
  `execution_node_cache()`.
- [x] Wave 5.4 — il node executor e il workspace executor usano i seam
  tipizzati per node cache e framebuffer pool.
- [x] Wave 5.5 — i runner batch/single-node usano il seam backend tipizzato.
- [x] Wave 5.6 — `RenderServices` rimosso: backend, superfici, cache,
  cataloghi e decoder sono dipendenze tipizzate direttamente sul contesto;
  il gate impedisce la reintroduzione del service locator.
- [x] Wave 3.5a — il percorso `RenderPipeline::render_program` materializza
  direttamente una `Scene` typed tramite `ProgramSampler::sample_scene`, senza
  creare `EvaluatedCompositionFrame`.
- [x] Wave 3.5b — il compiler boundary test verifica la materializzazione
  diretta della `Scene` typed dal sampler.
- [x] Wave 3 — il percorso typed del runtime campiona direttamente la `Scene`
  dal `CompiledProgram` e la consegna al grafo; il frame loop non materializza
  più `EvaluatedCompositionFrame`.
- [x] Wave 8 — `PreparedRenderJob` non possiede più uno stadio di evaluation:
  il lease viene passato al `RenderPipeline`, che coordina sampling, graph
  execution e output; il payload contiene solo stato typed/frame-local.
- [x] Wave 4 — target CMake separati di render-plan/graph builder/graph
  compiler assorbiti nel target `chronon3d_compiler`.
- [x] Wave 4 — `graph_compile.hpp` pubblico e free facade rimossi; il graph
  lowering passa da un seam interno al compiler.
- [x] Wave 4 — wrapper `GraphBuilder::build` e relativo translation unit
  eliminati; i test usano direttamente la pipeline dei compiler pass.
- [x] Wave 4 — `GraphBuildPipeline` è stato assorbito in
  `compiler::CompilerPassPipeline`; header, source e simbolo/path legacy sono
  stati rimossi.
- [x] Wave 4 — census finale: nessun caller production indipendente di
  `build_graph`, `compile_frame_graph` o `compile_render_plan` (verified
  2026-09-08 across `src/`, `apps/`, and `include/`; remaining matches are
  declarations, comments, compiler-root ownership, or tests).

## Demolition debt

- Owner: compiler/runtime maintainers.
- Reason: migrazione da render-plan/graph authorities separate verso il
  compiler root e `CompiledProgram`.
- Exit condition: i tre compatibility payload/entrypoint sopra elencati non
  compaiono più nel production execution path e i gate di boundary passano.
- Equivalence gate: compiler boundary, graph executor authority, CLI/IPC
  request parity, targeted render-graph tests e build `linux-fast-dev`.
- Removal scope: legacy CLI adapter, public graph facade, obsolete CMake
  sources/targets e compatibility payload non più referenziati.
- Status: `CLOSED` 2026-10-10 (verified `2c921aa3b`). Wave 1/2/3/4 payloads
  gone from the production execution path; Wave 4 graph facade
  demolition/caller census was already closed. Remaining
  `render_compiled_composition` / `render_compatibility*` entrypoints are
  public-API compat adapters (legacy prepared jobs, SDK file methods), not
  executor payload — removal would break API compat and is out of scope.

## Verified residual census — `be14cbd86` + delta 2026-09-09

The following are deliberate blockers, not documentation debt:

- `src/compiler/compiler.cpp` still calls `render_plan::compile_render_plan`
  and stores `PreparedRenderPlan` in `CompiledProgram`.
- `src/compiler/CMakeLists.txt` still owns render-plan and graph-builder
  compatibility translation units and links `chronon3d_render_plan`.
- `src/compiler/program_sampler.cpp` still reaches
  `detail::compiled_composition(program)`.
- `EvaluatedCompositionFrame`, `SceneProgramStore`, `SceneHasher`, and
  `RenderServices` still cross the runtime execution boundary.

`TypedProgramSampler` is present and boundary-tested, but is not yet a
production execution authority. No wave may be marked closed until the typed
parameter block is consumed by the graph executor and the legacy bridge is
deleted.

### FASE D26-D30 — census 2026-09-09 (compiler boundary)

- **D26 sources `src/compiler/**` — PARZIALE:** 30 TU sotto `src/compiler/**` (compiler, program_sampler, typed_program_sampler,
  frontend, lowering, graph, optimizer, fingerprint, `graph_builder/*`, `graph_compiler/*`, `optimizer/*`); `chronon3d_compiler`
  linker `chronon3d_graph_core + chronon3d_graph_nodes` (corretto — graph IR/nodes sono dipendenza del lowering).
- **D27 census header-per-header — FATTO:** `graph_builder_layer_passes.cpp` include `graph_builder_coordinates.hpp` +
  `evaluated_layer_placement.hpp` da `src/render_graph/builder/` — sono sotto `PRIVATE` include roots
  (`src/render_graph`, `src/render_graph/nodes`, `src/render_graph/builder`, `src/render_graph/builder/passes`,
  `src/render_graph/compiler`) in `src/compiler/CMakeLists.txt:39-43`. Sono coordinate già migrate concettualmente
  ma fisicamente ancora in `render_graph/builder/` — owner `chronon3d_compiler` già le compila via `src/compiler/graph_builder/**`
  (compat TU), il file header originale resta solo per include path compat finché non viene spostato fisicamente.
- **D28 reverse include roots — APERTO (intentional):** i `PRIVATE` roots sono 1-way `compiler → render_graph/*` per TU compat già migrati;
  non sono "reverse ownership" — il gate `configured_compiler_no_legacy_graph_sources` vieta a `chronon3d_compiler` di assorbire
  `src/render_graph/{builder,compiler,optimizer}/` come sources (deve usare `src/compiler/**`). Eliminazione progressiva = spostare
  fisicamente `graph_builder_coordinates.hpp/evaluated_layer_placement.hpp` in `src/compiler/graph_builder/` e aggiornare include.
- **D29 non spostare cosmeticamente — FATTO:** ogni header che conosce `RenderGraphNode`/`TextRunNode`/backend execution è
  correttamente classificato runtime/backend-dependent e resta `render_graph-owned`; solo i lowering puri sono `compiler-pure`.
  Boundary corretto > pathname cosmetico.
- **D30 exit condition `chronon3d_compiler sources = src/compiler/** && vendor-neutral && zero reverse ownership && codemodel PASS` — PARZIALE:**
  `src/compiler/**` puro + no CUDA/NVENC/NVDEC/vendor handles nel compiler (grep `cuda|nvenc|nvdec|vulkan` su `src/compiler/` = 0 include), ma i `PRIVATE` compat roots restano finché gli header originali non migrano fisicamente. Gate codemodel `configured_compiler_no_legacy_graph_sources` è l'evidence finale.

### FASE E31-E35 — census 2026-09-09 (RenderGraphContext collapse)

- **E31 15+ pointers — APERTO (censito):** `RenderGraphContext` espone `backend`, `surface_registry`, `text_render_resources`,
  `node_cache`, `framebuffer_pool`, `registry`, `video_decoder`, `effect_catalog`, `transition_catalog`, `precomp_builder` (10 execution deps)
  + `execution`/`frame_parameters`/`build_dependencies` (3) + `runtime`/`compiled_graph_cache`/`scheduler`/`asset_resolver`/`session`/`program_fingerprint` (6) = 19 campi. `FrameStaticContext` già estratto; `RenderServices` già demolito (Wave 5.6).
- **E32 production via `FrameExecution → JobSession → RuntimeContext` — PARZIALE:** `RenderPipeline::render()` già usa
  `JobSession::program()` → `RenderPipeline::render_program()` → `FrameExecution`/`frame_parameters`; `render_compatibility*`
  è l'unico path che tocca `compatibility_composition`. Collasso completo bloccato da `compatibility_composition` (FASE C).
- **E33 build/preflight via `GraphBuildDependencies` — FATTO:** `RenderGraphContext::build_dependencies` (const*) + `GraphBuildDependencies`
  già wiring per builder; nessun nuovo `services` bundle.
- **E34 eliminare direct pointer duplicati post-compatibility — DEFERRED:** esplicitamente rinviato a dopo morte di `compatibility_composition` (ticket WAVE-CLOSEOUT DEMOLITION debt). Togliere pointer ora = doppia migrazione.
- **E35 gate anti-service-locator — FATTO:** `tools/architecture_rules_cleanup.toml` `forbidden_symbol: render_services_locator_demolished` (scan `include/src/apps/tests`, `\bRenderServices\b` = 0) + `graph_builder_no_execution_service_locator` (`services.` in `src/render_graph/builder` = 0). Reintroduzione = GATE FAIL.

### FASE G46-G48 — measurement notes 2026-09-09

- **G46 `SceneProgramStore` contention — NOT MEASURED (note locked):** `SceneProgramStore` (`src/render_graph/cache/scene_program_store.cpp`)
  single `std::mutex` per-bucket; non è stato misurato sotto executor parallelo. Se costoso: first option `per-entry locking`,
  second `bounded sharding` — mai nuovo cache engine. Misurare prima di toccare.
- **G47 `DeviceScheduler` contention — NOT MEASURED (note locked):** `DeviceScheduler::reserve()` path non profilato; misurare lock hold/wait prima di refactor.
- **G48 `FramebufferPool` sharding — SUPERSEDED (measurement 2026-10-02):**
  la nuova prova condivisa a 8 worker ha misurato `wait_us=35247`, `hold_us=399`
  prima dell’intervento, quindi il precedente closeout “sharding non
  giustificato” non descrive più il codice. Il pool ora usa 64 worker-local
  slots per il riuso caldo, mantenendo budget byte, cap per classe e LRU
  globali attraverso contabilità atomica e scansioni di manutenzione. La stessa
  prova dopo il cambio misura `wait_us=0`, `hold_us=3`; vedere
  `TICKET-HOTSPOT-DECOMPOSITION.md` per lo stato dei test. Run successivi
  variavano tra `wait_us=0..34`, `hold_us=3..8` per rumore di sistema.
- **G42 benchmark text-heavy — NOT RUN:** corpus B00-B11 esiste; metriche target `allocations/frame, bytes/frame, text ms/frame, FPS`
  da produrre con benchmark gate (render_graph `graph_cache_refresh_fallback_total` già counter per refresh fallback).

### FASE H49-H53 — Vulkan recursive_mutex 2026-09-09

- **H49-H51 call-graph — CENSUS DONE / DEFERRED:** `VulkanBackend::Impl::api_mutex` era unico `recursive_mutex`; dalla sharding P1 ora sono
  3 shard non-ricorsivi (`surface_mutex → upload_mutex → submission_mutex`, `Impl::wait_for_standalone()` + `wait_for_pending()` split).
  I 2 siti ricorsivi residui sono SDK `sdk_render_engine.cpp` state + Vulkan backend API shim — conversione a `api_mutex/api_locked`
  pattern (`public_api(){lock_guard lock(api_mutex); return api_locked();}`) è DEFERRED after green baseline: richiede re-entry test + GPU cert rerun (deadlock risk). Spec in TICKET-VK-LOCK-SHARDING.
- **H52-H53 re-entry test — DEFERRED:** test di re-entry prima della sostituzione + intera GPU certification dopo la sostituzione;
  non eseguire finché baseline non è green.

## Verification snapshot

- Build `linux-fast-dev` dei target compiler, graph pipeline, CLI render e test
  boundary: `PASS`.
- Gate architetturale: `PASS` (122/122 regole).
- Gate compiler/executor/runtime ownership: `PASS`.
- `compiler_boundary`, `render_job_contract` e il test isolato tile: `PASS`.
- Suite completa `render_graph`: `FAIL` pre-esistente/non correlato alle aree
  modificate; il test `Pinned absolute TextRun stays centered across animation
  boundary` fallisce con 4 assertion anche in esecuzione isolata. Non viene
  marcata verde.

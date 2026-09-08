# Compiler Demolition Waves — Wave 1–4

Status: `ACTIVE` (verified against `be14cbd86`; Waves 1/2/3/4 execution
demolition remains open)

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
- [ ] Wave 3 — il render-plan runtime esegue direttamente topology e parameter
  programs di `CompiledProgram`, senza materializzare `PreparedRenderPlan` /
  `CompiledComposition` come payload dell’executor.
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
- [x] Wave 5.1 — il compiled executor accede al backend tramite il seam
  tipizzato `RenderGraphContext::execution_backend()` invece del bundle
  `RenderServices`.
- [x] Wave 5.2 — il lifecycle executor dei framebuffer usa il seam tipizzato
  `execution_framebuffer_pool()`.
- [x] Wave 3.5a — il percorso `RenderPipeline::render_program` materializza
  direttamente una `Scene` typed tramite `ProgramSampler::sample_scene`, senza
  creare `EvaluatedCompositionFrame`.
- [ ] Wave 3 — il runtime esegue il programma tipizzato direttamente. Il
  `ProgramSampler` attuale è ancora un adapter verso
  `detail::compiled_composition(program)` e quindi non costituisce la chiusura
  typed della wave.
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
- Status: `ACTIVE` for the remaining Wave 1/2/3 compatibility payloads and
  execution callers; Wave 4 graph facade demolition/caller census is closed.
  The metadata evacuation is complete, but the bridge itself remains `ACTIVE`
  until execution no longer materializes a `CompiledComposition`.

## Verified residual census — `be14cbd86`

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

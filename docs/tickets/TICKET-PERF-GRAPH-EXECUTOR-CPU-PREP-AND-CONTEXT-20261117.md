# TICKET-PERF-GRAPH-EXECUTOR-CPU-PREP-AND-CONTEXT-20261117 — Parallelismo CPU-prep nei livelli del RenderGraph su backend Vulkan e azzeramento costo `clone_for_node_execution`

- **Stato:** CLOSED (2026-11-17)
- **Priorità:** P2 (Ottimizzazione architetturale CPU/Executor — coordinato con `TICKET-HOTPATH-ZERO-ALLOC-P1` D3 e `ADR-DRAFT-NODE-CONTEXT-OWNERSHIP`)
- **Ambito:** `src/render_graph/executor/executor_level_dispatch.cpp`, `src/render_graph/executor/node_runner_single_node_detail.hpp`
- **Owner:** C3D RenderGraph executor maintainer
- **Allineamento piano:** `docs/CHRONON_PLAN.md` Stage 3 / P2-06 e `docs/tickets/TICKET-HOTPATH-ZERO-ALLOC-P1.md` (D3)

---

## 1. Problema e Evidenza nel Codice

Nel dispatcher dei livelli del RenderGraph (`src/render_graph/executor/executor_level_dispatch.cpp` e `src/render_graph/executor/node_runner_single_node_detail.hpp`) restano due colli di bottiglia lato CPU quando un grafo presenta molti nodi per livello:

1. **Forzatura sequenziale dell'intero livello quando il backend usa Native Surfaces** (`executor_level_dispatch.cpp:40-49`):
   ```cpp
   const bool native_surface_backend =
       detail::has_frame_scoped_native_surfaces(ctx.backend_stats_api());
   const bool use_parallel = should_parallelize_render_level(
       level.size(), scheduler.concurrency(), native_surface_backend);
   ```
   Poiché `VulkanBackend::Impl` registra su un singolo `VkCommandBuffer` per frame slot (che richiede sincronizzazione esterna secondo la specifica Vulkan), `should_parallelize_render_level` disabilita il parallelismo di livello per tutti i job GPU nativi.
   Tuttavia, diversi nodi del grafo eseguono **lavoro CPU non banale prima di registrare il comando Vulkan** (es. proiezione CPU dei vertici e calcolo bounding box per le mesh, preparazione/risoluzione layout testo, valutazione cache e predicted bbox). Con il livello forzato sequenziale, tutto questo lavoro CPU preliminare gira su **1 solo core** mentre il pool di worker TBB resta inattivo.

2. **Clonazione `RenderGraphContext::clone_for_node_execution()` per ogni nodo e fused batch** (`node_runner_single_node_detail.hpp:136` e `:226`):
   ```cpp
   RenderGraphContext node_ctx = ctx.clone_for_node_execution();
   ```
   Come documentato in `docs/tickets/TICKET-HOTPATH-ZERO-ALLOC-P1.md` (voce D3) e in `docs/tickets/ADR-DRAFT-NODE-CONTEXT-OWNERSHIP.md`, ogni invocazione di `clone_for_node_execution()` copia l'intero `RenderGraphContext`, inclusi 3 `std::shared_ptr` (`static_context`, `frame_error`, `processor_snapshot`), causando operazioni atomiche `LOCK XADD` (incremento e decremento del reference count al termine dello scope del nodo) per ogni nodo × frame.

---

## 2. Come Chiudere Completamente il Ticket (Step-by-Step)

### Parte 1 — Chiusura di D3 (`clone_for_node_execution` zero-refcount)
Seguire il percorso già tracciato in `docs/tickets/ADR-DRAFT-NODE-CONTEXT-OWNERSHIP.md` e `TICKET-HOTPATH-ZERO-ALLOC-P1.md`:
1. **Misura preliminare**: profilare il costo di `clone_for_node_execution()` su un grafo ad alto numero di nodi (es. overlay matrix / documentary sequence con 50+ nodi per frame).
2. **Separazione Owner vs View**:
   - L'executor del frame mantiene in vita per l'intera durata di `execute_levels()` i payload condivisi (`static_context`, `processor_snapshot`, ecc.).
   - Nei cloni worker-local per-nodo (`clone_for_node_execution`), puntare a tali strutture immutabili tramite puntatori raw non-owning (`const StaticContext*`, `const ProcessorSnapshot*`) o passare ai nodi una vista leggera `NodeExecutionScope` che contiene solo i campi mutabili per-nodo (`node_exec`: `text_bbox_reporter`, `sampled_layer`, `planned_physical_slot`, `current_identity`, `reusable_bottom`).
3. **Effetto**: zero incrementi/decrementi atomici di `std::shared_ptr` durante il dispatch dei nodi.

### Parte 2 — Two-Phase Level Dispatch per Backend Native-Surface (CPU Prep Parallela + GPU Record Deterministico)
Solo per i livelli con `level.size() >= kParallelThreshold` su backend `native_surface_backend`:
1. **Fase 1 (Parallela sui worker TBB, read-only sul backend)**:
   - Calcolare in parallelo per tutti i `node_id` del livello:
     - `evaluate_cache(...)` (`node_runner_single_node_detail.hpp:177`);
     - `node.predicted_bbox(...)` (`node_runner_single_node_detail.hpp:195`);
     - eventuale preparazione CPU dei payload del nodo (es. trasformazione/proiezione buffer vertici in memoria host locale al nodo o preparazione glyph run).
2. **Fase 2 (Sequenziale in ordine rigoroso `level_index = 0 .. level.size() - 1`)**:
   - Eseguire la registrazione effettiva sul `VkCommandBuffer` attivo e sulle superfici del frame slot in ordine 100% deterministico, senza alcuna contesa di lock sul command buffer Vulkan e preservando l'ordine esatto dei pass richiesti da `CompiledResourceTable` (`frame_batch.pass_count`).

---

## 3. Demolition Debt

1. **Owner**: C3D RenderGraph executor maintainer.
2. **Reason**: Durante la migrazione di `RenderGraphContext` (D3), se viene mantenuto un metodo wrapper `clone_for_node_execution()` per compatibilità con i test unitari che istanziano `RenderGraphContext` direttamente fuori da `GraphExecutor`.
3. **Exit condition**: Tutti i test unitari usano il costruttore/scope canonico e nessun call site invoca la copia profonda di `shared_ptr`.
4. **Equivalence test/gate**: `chronon3d_render_graph_tests` (421/421), `chronon3d_determinism_tests` e corpus golden con output bit-identico.
5. **Removal scope**: Campi `shared_ptr` duplicati nei cloni per-nodo in `include/chronon3d/render_graph/render_graph_context.hpp`.
6. **Status**: `ACTIVE`.

---

## 4. Verifica e Criteri di Accettazione (Definition of Done)

1. **Determinismo dell'Ordine di Sottomissione Vulkan**:
   - L'ordine dei `pass_index` registrati nel `VkCommandBuffer` (`frame_batch.pass_count`) deve restare **identico bit-per-bit** tra esecuzione seriale e two-phase (verificato tramite `emit_plan_pass_transitions` e hash dei frame).
2. **Zero `LOCK XADD` da `shared_ptr` in `execute_single_node`**:
   - Verifica ispettiva dell'assembly / profilo `perf` di `execute_single_node`: nessuna copia di `std::shared_ptr` nel prologo/epilogo del nodo quando `cache_hit_fast_path` o l'esecuzione normale vengono attraversati.
3. **Suite Obbligatorie Verdi**:
   - `chronon3d_render_graph_tests`
   - `chronon3d_backend_registry_tests`
   - `chronon3d_compositor_tests`
   - `chronon3d_render_job_contract_tests`
   - Developer gates (`tools/check_architecture.py` 218/218).

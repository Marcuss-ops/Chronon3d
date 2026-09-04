# TICKET-MAIN-TRUTH-STAGE-1-2 — main truth + Wave 0 closeout

**Stato:** VERIFY  
**Priorità:** P0  
**Data:** 2026-09-04  
**Baseline censita:** `main@eb56a6f6c59e6d5ae3eefa1f1cbd69e3ee1f58d1`

## Obiettivo

Rendere il `main` remoto la sola verità operativa prima di qualsiasi nuova architettura: clean-checkout build consistency, chiusura della migrazione frame-slot, coerenza del Vulkan pipeline cache e census finale della Wave 0.

## Finding P0 — pipeline cache

`src/backends/vulkan/vulkan_backend_lifecycle_private.cpp` crea un `VkPipelineCache`, lo passa a tutti i `vkCreateComputePipelines` e ne salva i dati su disco attraverso `kernels.pipeline_cache`.

Sul baseline censito, `src/backends/vulkan/vulkan_kernel_store.hpp` non dichiarava più `pipeline_cache`. Il lifecycle e il suo owner erano quindi incoerenti nel clean checkout.

### Fix

- `VulkanKernelStore` possiede esplicitamente `VkPipelineCache pipeline_cache{VK_NULL_HANDLE}`.
- `VulkanKernelStore::destroy()` distrugge e azzera il cache handle insieme agli altri oggetti Vulkan di sua ownership.
- La dipendenza standard-library necessaria al persistence path (`std::filesystem`) è resa disponibile dal boundary privato Vulkan invece di dipendere accidentalmente da include transitivi non correlati.
- `test_vulkan_kernel_store.cpp` blocca l'esistenza dell'owner e il suo stato iniziale nullo.

Questo fix NON implementa il futuro hardening del persistent cache (UUID/device keying, ABI fingerprint, corruption handling, atomic save, daemon concurrency). Quello resta Stage 7.

## Frame-slot migration census

Observed on `main`:

- `include/chronon3d/runtime/frame/frame_execution_slot.hpp` presente.
- `FrameSlotPool`, `GpuCompletionTracker`, `FrameQueue` presenti.
- `src/runtime/CMakeLists.txt` registra i tre TU authority (`frame_slot_pool.cpp`, `gpu_completion_tracker.cpp`, `frame_queue.cpp`).
- Il vecchio `FrameSlotPipeline` è stato rimosso dalla history di cleanup.
- Il vecchio `frame_execution_slot_ring.hpp` non è più parte del tree corrente.

**Verdetto:** migrazione strutturale chiusa. Non reintrodurre il facade ritirato per soddisfare nomi storici nelle note.

## Wave 0 census

| Area | Verdetto | Evidenza/azione |
|---|---|---|
| Surface authority | DONE | `CompiledResourceTable` è il placement authority; Vulkan è materialization/binding. |
| GraphExecutor | DONE | unica authority produttiva; helper executor hanno ownership distinte. |
| Descriptor authority | DONE | `VulkanDescriptorAuthority` possiede allocazione/handle; alias in `Impl` sono riferimenti compatibility-only. |
| FFmpeg subprocess | OPEN demolition debt | `FfmpegPipeSink` resta solo come path non-native; la cancellazione fisica è vietata finché `TICKET-FFMPEG-PIPE-SINK-DEMOLITION` non soddisfa tutte le exit conditions. |
| DeviceScheduler | DONE | implementazione + focused tests esistono. |
| CLI typed config / RenderPlan | DONE focused | il path CLI V3/RenderPlan resta canonico. |
| Telemetry | PARTIAL / contained | default-path store recording gated sul consumer SQLite reale; delete fisico residuo separato. |
| Docs | DONE in this closeout | `CURRENT_STATUS`, `CHRONON_PLAN`, `FOLLOWUP_TICKETS` riallineati. |

## Acceptance criteria

- [x] Caller/owner census frame-slot eseguito.
- [x] Negative census: non ripristinare `FrameExecutionSlotRing`/`FrameSlotPipeline` legacy.
- [x] Pipeline-cache owner coerente con i caller del lifecycle.
- [x] Regression lock sul `VulkanKernelStore` aggiornato.
- [x] Wave 0 census documentato senza demolizioni premature.
- [x] Canonici principali riconciliati.
- [ ] `Chronon CI` green sullo SHA risultante di `main`.
- [ ] Post-push main-tip verification registrata dopo il commit.

## Close rule

Passare `VERIFY -> DONE` solo quando il commit di questo closeout è realmente il tip di `main` e la CI same-SHA è verde. Il ticket FFmpeg subprocess resta indipendente: questo ticket può chiudere anche con quel demolition debt OPEN, purché il path sia classificato e non sia una seconda production authority nei build native.

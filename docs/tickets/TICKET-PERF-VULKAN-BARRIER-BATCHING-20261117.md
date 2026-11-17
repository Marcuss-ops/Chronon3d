# TICKET-PERF-VULKAN-BARRIER-BATCHING-20261117 — Batching delle `VkImageMemoryBarrier2KHR` per pass, deduplicazione `emit_unplanned_compute_sync` e restrizione `ALL_COMMANDS_BIT`

- **Stato:** CLOSED (2026-11-17 — verified in `build/chronon/linux-video-test`)
- **Priorità:** P1/P2 (Allineato al P0 `TICKET-RENDER-STACK-HARDENING-20261106` W1)
- **Ambito:** `src/backends/vulkan/vulkan_backend_sync_private.cpp`, `src/backends/vulkan/vulkan_backend_submission_private.cpp`
- **Owner:** C3D Vulkan backend owner
- **Allineamento piano:** `docs/CHRONON_PLAN.md` Stage 5 (Single synchronization authority) e `docs/tickets/TICKET-RENDER-STACK-HARDENING-20261106.md` (W1)

---

## 1. Problema e Evidenza nel Codice

L'audit di `src/backends/vulkan/vulkan_backend_sync_private.cpp` e `src/backends/vulkan/vulkan_backend_submission_private.cpp` evidenzia tre inefficienze strutturali nella generazione delle barriere Vulkan:

1. **Emissioni non-batched (1 chiamata `vkCmdPipelineBarrier2KHR` per singola immagine)**:
   - In `emit_plan_pass_transitions()` (`vulkan_backend_sync_private.cpp:296-318`), `emit_unplanned_compute_sync()` (`vulkan_backend_sync_private.cpp:320-346`) e `emit_command_batch_boundary()` (`vulkan_backend_sync_private.cpp:411-432`), ogni singola immagine invoca `emit_resource_transition()`, che costruisce una `VkDependencyInfoKHR` con `imageMemoryBarrierCount = 1` e chiama `vkCmdPipelineBarrier2KHR`.
   - In un pass con 4 immagini (es. `draw_inline_mesh` con `dst_image`, `texture_image`, `mr_image`, `depth_image`), vengono registrati 4 comandi `vkCmdPipelineBarrier2KHR` consecutivi nel command buffer invece di 1 solo comando con 4 barriere.
2. **Doppia sincronizzazione sulle superfici già pianificate in `emit_pass_sync()`** (`vulkan_backend_sync_private.cpp:376-389`):
   ```cpp
   if (frame_batch.command_plan) {
       emit_plan_pass_transitions(command, *frame_batch.command_plan,
                                  frame_batch.pass_count);
       emit_unplanned_compute_sync(command, images);
       return;
   }
   ```
   Quando `frame_batch.command_plan` è attivo, `emit_plan_pass_transitions` emette già le transizioni compilate per le superfici del piano (es. `dst_image`), ma la riga successiva `emit_unplanned_compute_sync(command, images)` riemette una seconda barriera `ComputeShader -> ComputeShader` su **tutte** le immagini della lista `images`, incluse quelle appena sincronizzate dal piano! Il commento a riga 382 spiega che `emit_unplanned_compute_sync` serve solo per le texture asset promosse fuori dal grafo compilato (`ImageCache`/`GpuAssetCache`), ma il codice non filtra le superfici già presenti nel piano.
3. **Wait stage `VK_PIPELINE_STAGE_ALL_COMMANDS_BIT` sull'acquisizione di superfici CUDA** (`vulkan_backend_submission_private.cpp:99`):
   ```cpp
   wait_stages.push_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
   ```
   Come nota il commento stesso a riga 97 ("Reused encoder surfaces can be cleared/copied by transfer before compute"), gli unici stage reali che toccano la superficie in Vulkan sono `VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT` (esattamente come usato nella barriera di ownership transfer in `vulkan_backend_sync_private.cpp:456-457`). Usare `ALL_COMMANDS_BIT` blocca l'intero top-of-pipe della queue al semaforo CUDA.

---

## 2. Come Chiudere Completamente il Ticket (Step-by-Step)

### Passo 1 — Introdurre `build_image_barrier2` ed `emit_image_barriers2(std::span<const VkImageMemoryBarrier2KHR>)`
In `src/backends/vulkan/vulkan_backend_sync_private.cpp`:
1. Estrarre la costruzione di `VkImageMemoryBarrier2KHR` da `emit_resource_transition()` in una funzione pura `VkImageMemoryBarrier2KHR to_vk_image_barrier2(VkImage image, const runtime::ResourceTransition& record, std::uint32_t queue_family)`.
2. Creare `VulkanBackend::Impl::emit_image_barriers2(VkCommandBuffer command, std::span<const VkImageMemoryBarrier2KHR> barriers)` (simmetrica a `emit_buffer_barriers2` alla riga 393) che invoca **una sola volta** `pfn_cmd_pipeline_barrier2(command, &dependency)` con `imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size())` e incrementa `stats.barriers_emitted += barriers.size()`.
3. In `emit_plan_pass_transitions`, `emit_unplanned_compute_sync`, `emit_command_batch_boundary` e `emit_cuda_surface_releases`, raccogliere le barriere in un buffer stack-local (es. `std::array<VkImageMemoryBarrier2KHR, 16>`) o in uno scratch vector pre-riservato di `Impl` ed emetterle in un'unica chiamata `emit_image_barriers2`.

### Passo 2 — Evitare barriere duplicate in `emit_pass_sync` quando `command_plan` è attivo
In `src/backends/vulkan/vulkan_backend_sync_private.cpp:376-389`:
- Raccogliere i `VkImage` già coperti da `emit_plan_pass_transitions` per `frame_batch.pass_count` (un piccolo array stack di max 8 `VkImage` toccati in quel pass dal piano).
- Quando si scorre `images` per le risorse fuori piano (`ImageCache`/`GpuAssetCache`), saltare ogni `const Image* img` il cui `img->image` è già stato incluso nelle transizioni compilate di quel `pass_index` (o fare un'unica chiamata `emit_image_barriers2` che fonde le barriere pianificate del pass + le barriere per i soli `Image*` non presenti nel pass del piano).

### Passo 3 — Restringere `wait_stages` in `VulkanBackend::Impl::submit()`
In `src/backends/vulkan/vulkan_backend_submission_private.cpp:99`:
- Sostituire `VK_PIPELINE_STAGE_ALL_COMMANDS_BIT` con:
  ```cpp
  wait_stages.push_back(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                        VK_PIPELINE_STAGE_TRANSFER_BIT);
  ```
- Verificare e documentare `VulkanBackend::synchronize_native_dependencies()` (`vulkan_backend_sync_private.cpp:523-550`) secondo il piano W1 di `TICKET-RENDER-STACK-HARDENING-20261106.md`.

---

## 3. Demolition Debt

Per ogni eventuale barriera conservativa mantenuta temporaneamente per risorse fuori piano (`emit_unplanned_compute_sync`), applicare la scheda Demolition Debt già prescritta in `TICKET-RENDER-STACK-HARDENING-20261106.md`:
1. **Owner**: C3D Vulkan backend owner.
2. **Reason**: Texture asset promosse via `ImageCache`/`GpuAssetCache` non ancora dichiarate nella `CompiledResourceTable` del command plan.
3. **Exit condition**: Tutte le texture lette da un pass sono registrate come `ResourceUse` nel compilatore del grafo.
4. **Equivalence test/gate**: Lane SyncVal con 0 errori sul corpus 1080p testo/immagine + CUDA/NVENC + camera.
5. **Removal scope**: `emit_unplanned_compute_sync` in `src/backends/vulkan/vulkan_backend_sync_private.cpp`.
6. **Status**: `ACTIVE`.

---

## 4. Verifica e Criteri di Accettazione (Definition of Done)

1. **Zero errori SyncVal (`VK_LAYER_KHRONOS_synchronization2` + Validation Layer con SyncVal attivo)**:
   - Eseguire il corpus W1 (sequenza 1080p testo/immagine, run CUDA/NVENC e coppia camera) con SyncVal abilitato: **0 errori VUID / SyncVal**.
2. **Riduzione misurata delle chiamate e delle barriere**:
   - Confrontare `stats.barriers_emitted` prima e dopo la deduplicazione su un piano compilato: verificare che le superfici pianificate del pass non ricevano più la doppia barriera da `emit_unplanned_compute_sync`.
3. **Suite pre-push e parità**:
   - `chronon3d_backend_registry_tests`, `chronon3d_compositor_tests`, `chronon3d_render_job_contract_tests`, `chronon3d_render_graph_tests` verdi.
   - Hash dei frame raw byte-identici prima/dopo.

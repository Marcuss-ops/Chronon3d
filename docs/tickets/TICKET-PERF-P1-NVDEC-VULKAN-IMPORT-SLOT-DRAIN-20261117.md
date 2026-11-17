# TICKET-PERF-P1-NVDEC-VULKAN-IMPORT-SLOT-DRAIN-20261117 — P1: Stallo globale `wait_for_pending_submissions()` al riciclo di uno slot nel ring NVDEC→CUDA→Vulkan

- **Stato:** CLOSED (2026-11-17 — verified in `build/chronon/linux-video-test`)
- **Priorità:** P1 (Stallo periodico dell'intera pipeline GPU su export video con NVDEC)
- **Verifica sul codice attuale:** **PROBLEMA VERO (`[CODICE]` verificato)**
- **File coinvolti:**
  - `src/backends/vulkan/vulkan_cuda_frame_importer.cpp` (righe 57–69)
  - `src/backends/vulkan/vulkan_backend_submission_private.cpp` (righe 35–67)
- **Owner:** C3D Vulkan / Video interop maintainer

---

## 1. Diagnosi Esatta del Problema

In `src/backends/vulkan/vulkan_cuda_frame_importer.cpp:57-69`, `VulkanCudaFrameImportSession::import()` gestisce un ring di `slots_` (6 slot, pari a `backend.native_surface_ring_capacity()`):

```cpp
auto& slot = slots_[next_slot_++ % slots_.size()];
if (slot.pending) {
    if (!backend_.wait_for_pending_submissions() ||
        !slot.compositor || !slot.compositor->synchronize()) {
        spdlog::error("[native-import] pending slot {} generation {} did not drain",
                      slot.index, slot.generation);
        return nullptr;
    }
    slot.pending = false;
}
```

Quando il ring compie un giro completo (al frame 6, 12, 18, ...), `slot.pending` è `true` perché quello slot è stato importato 6 frame prima.
Per assicurarsi che Vulkan abbia finito di leggere `slot.surface` prima che CUDA ci scriva il nuovo frame decodificato da NVDEC, il codice invoca `backend_.wait_for_pending_submissions()`, che chiama `VulkanBackend::Impl::wait_for_pending()` (`src/backends/vulkan/vulkan_backend_submission_private.cpp:35-67`).

**Difetto strutturale**: `wait_for_pending()` **non** attende solo il completamento del frame $N-6$ associato a `slot.surface`, ma cicla su **tutti i 6 slot del `FrameBatchState`** (`i = 0 .. kSlotCount - 1`) eseguendo `vkWaitForFences(..., UINT64_MAX)` su ciascuno, oltre ad attendere tutti gli slot di upload (`wait_upload_slot`).
Questo significa che ogni 6 frame la pipeline CPU→Vulkan→CUDA viene **completamente drenata e azzerata**, vanificando il pipelining asincrono dei 5 frame più recenti ($N-5 \dots N-1$).

---

## 2. Come Risolvere e Bonificare Completamente il Problema

### Passo 1 — Esporre un metodo di attesa granulare per singola superficie su `VulkanBackend`
In `include/chronon3d/backends/vulkan/vulkan_backend.hpp` e `src/backends/vulkan/vulkan_backend_submission_private.cpp`:
1. `VulkanSurfaceStore` traccia già in `surfaces.recording_slot_masks[handle]` la maschera di bit (`std::uint32_t slot_mask`) degli slot del `FrameBatchState` (`0 .. 5`) che hanno referenziato `handle` e sono ancora `in_flight`.
2. Aggiungere `bool VulkanBackend::wait_for_surface_idle(runtime::RenderSurfaceHandle handle) noexcept`:
   - Legge `slot_mask = surfaces.recording_slot_mask(handle)`.
   - Per ogni indice `i` (`0 .. FrameBatchState::kSlotCount - 1`) il cui bit `(1u << i)` è attivo in `slot_mask`:
     - Se `frame_batch.in_flight[i]` è vero, attende **solo** `frame_batch.fences[i]` con `vkWaitForFences(device, 1, &frame_batch.fences[i], VK_TRUE, UINT64_MAX)`, resetta quella fence, marca `frame_batch.in_flight[i] = false`, legge i timestamp `read_gpu_timestamps(i)` e ritira le superfici transienti `retire_frame_slot_transient_surfaces(i)`.
   - Se `slot_mask == 0` ma `pending_timeline_value != 0` (caso standalone non-batched), esegue `wait_for_standalone()`.
   - In questo modo, al frame $N$, il riciclo dello slot $N-6$ attende **esclusivamente** il fence del frame $N-6$ (che nella quasi totalità dei casi è già `VK_SUCCESS` al polling immediato!), lasciando i frame $N-5 \dots N-1$ in esecuzione parallela sulla GPU.

### Passo 2 — Aggiornare `VulkanCudaFrameImportSession::import()`
In `src/backends/vulkan/vulkan_cuda_frame_importer.cpp:61`:
- Sostituire `if (!backend_.wait_for_pending_submissions() || ...)` con:
  ```cpp
  if (!backend_.wait_for_surface_idle(slot.surface) ||
      !slot.compositor || !slot.compositor->synchronize()) {
  ```
- Mantenere `backend_.wait_for_pending_submissions()` **solo** nel distruttore `~VulkanCudaFrameImportSession()` (riga 37), dove il drain completo prima di distruggere le superfici è corretto.

---

## 3. Demolition Debt

Nessuno: `wait_for_pending_submissions()` rimane per il teardown di fine sessione; il percorso per-frame usa la stessa `VulkanSubmissionAuthority` con granularità per-slot.

---

## 4. Verifica e Criteri di Chiusura

1. **Verifica Contatori Sidecar (`*.timing.json`)**:
   - Su un export video con sorgente NVDEC di almeno 120 frame, verificare che `frame_batch_drain_wait_count` e `frame_batch_drain_wait_us` crollino (nessun drain forzato dei 6 slot ogni 6 frame).
2. **Test di Regressione e Teardown**:
   - `chronon3d_native_decoder_teardown_tests`, `chronon3d_backend_registry_tests` e `tools/verify_zero_copy_end_to_end.sh` 100% PASS con 0 errori SyncVal / CUDA.

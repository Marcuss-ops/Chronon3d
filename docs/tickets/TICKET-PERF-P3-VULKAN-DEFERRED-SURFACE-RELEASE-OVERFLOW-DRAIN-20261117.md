# TICKET-PERF-P3-VULKAN-DEFERRED-SURFACE-RELEASE-OVERFLOW-DRAIN-20261117 — P3: Sostituzione del drain totale `wait_for_pending()` con retirement granulare del singolo slot più vecchio in `flush_deferred_surface_releases`

- **Stato:** CLOSED (2026-11-17 — verified in `build/chronon/linux-video-test`)
- **Priorità:** P1 (Eliminazione micro-stutter da garbage collection delle superfici Vulkan)
- **Verifica sul codice attuale:** **PROBLEMA VERO (`[CODICE]` verificato)**
- **File coinvolti:**
  - `src/backends/vulkan/vulkan_surface_store_private.cpp` (righe 116–175)
  - `src/backends/vulkan/vulkan_backend_submission_private.cpp` (righe 35–67)
- **Owner:** C3D Vulkan backend maintainer

---

## 1. Diagnosi Esatta del Problema

In `src/backends/vulkan/vulkan_surface_store_private.cpp:134-138`, dentro `VulkanBackend::Impl::flush_deferred_surface_releases()`:

```cpp
// Bounded capacity: fallback wait if accumulated deferred releases exceed cap
static constexpr std::size_t kOverflowCap = 2 * FrameBatchState::kSlotCount;
if (surfaces.note_overflow_if_needed(kOverflowCap)) {
    wait_for_pending();
}
```

**Difetto verificato**:
1. Con `FrameBatchState::kSlotCount = 6`, la soglia `kOverflowCap` è pari a **12 handle**.
2. Quando il grafo usa superfici ausiliarie non pianificate o rilascia più di 2–3 superfici per frame mentre 4–5 frame sono in volo sulla GPU, `deferred_surface_releases.size()` supera 12.
3. A quel punto `wait_for_pending()` blocca la CPU finché **tutti i 6 slot del ring** non hanno finito sulla GPU, svuotando l'intera coda di esecuzione invece di attendere semplicemente il completamento del **singolo slot in volo più vecchio** per liberare le sue superfici!

---

## 2. Come Risolvere e Bonificare Completamente il Problema

### Passo 1 — Introdurre `wait_for_oldest_in_flight_slot()` in `VulkanBackend::Impl`
In `src/backends/vulkan/vulkan_backend_submission_private.cpp` e `src/backends/vulkan/vulkan_backend_impl.hpp`:
1. Tracciare per ogni slot `i` (`0 .. kSlotCount - 1`) in `FrameBatchState` il numero d'ordine di submit (`submitted_timeline_values[i]` o `submitted_frame_indices[i]`).
2. Implementare `bool VulkanBackend::Impl::wait_for_oldest_in_flight_slot()`:
   - Trova lo slot `oldest_i` con `frame_batch.in_flight[oldest_i] == true` e valore di submit minimo.
   - Se non c'è alcuno slot `in_flight`, chiama `wait_for_standalone()` e ritorna.
   - Altrimenti esegue `vkWaitForFences(device, 1, &frame_batch.fences[oldest_i], VK_TRUE, UINT64_MAX)` **solo su `oldest_i`**, resetta `frame_batch.fences[oldest_i]`, imposta `frame_batch.in_flight[oldest_i] = false`, esegue `read_gpu_timestamps(oldest_i)` e `retire_frame_slot_transient_surfaces(oldest_i)`.

### Passo 2 — Usare il retirement progressivo in `flush_deferred_surface_releases()`
In `src/backends/vulkan/vulkan_surface_store_private.cpp:134-138`:
1. Alzare `kOverflowCap` da `2 * FrameBatchState::kSlotCount` (12) a `8 * FrameBatchState::kSlotCount` (48 handle, dato che si tratta di soli metadati/handle in attesa che i rispettivi slot finiscano, mentre le immagini fisiche nel pool vengono già riutilizzate).
2. Quando `surfaces.note_overflow_if_needed(kOverflowCap)` scatta, chiamare `wait_for_oldest_in_flight_slot()` anziché `wait_for_pending()`, mantenendo gli altri 5 slot del ring attivi sulla GPU.

---

## 3. Demolition Debt

Nessuno.

---

## 4. Verifica e Criteri di Chiusura

1. **Verifica Contatori**:
   - Monitorare `overflow_wait_count`, `pending_release_high_water` e `deferred_release_drain_wait_us` nel sidecar su scene multi-layer con DoF/effetti: `deferred_release_drain_wait_us` deve ridursi drasticamente e non causare drain completi della pipeline.
2. **Suite di Test**:
   - `chronon3d_backend_registry_tests`, `chronon3d_render_graph_tests` e `chronon3d_effects_tests` verdi.

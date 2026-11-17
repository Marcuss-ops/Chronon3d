# TICKET-PERF-P2-DIRECT-YUV-PINNED-HOST-BUFFER-SYNC-20261117 — P2: Protezione con `CUevent` dei buffer host pinned in `CudaNv12Compositor::composite_direct_nv12_batch`

- **Stato:** CLOSED (2026-11-17 — verified in `build/chronon/linux-video-test`)
- **Priorità:** P1 (Prevenzione data-race DMA host→device sotto esecuzione multi-frame asincrona)
- **Verifica sul codice attuale:** **PROBLEMA VERO (`[CODICE]` verificato)**
- **File coinvolti:**
  - `src/media/video/compositor/cuda/cuda_nv12_compositor_batch.cpp` (righe 30–54, 99–126)
  - `src/media/video/compositor/cuda/cuda_nv12_compositor_detail.hpp`
- **Owner:** C3D Video Compositor / CUDA maintainer

---

## 1. Diagnosi Esatta del Problema

In `src/media/video/compositor/cuda/cuda_nv12_compositor_batch.cpp:34-54` e `:112-115`:

```cpp
const int slot = layer_batch_slot_;
layer_batch_slot_ = (slot + 1) % kLayerBatchSlots; // kLayerBatchSlots = 2
...
auto* host_layers = reinterpret_cast<DirectYuvLayerHost*>(layer_batch_host_[slot]);
for (const auto& instance : batch.instances) {
    ...
    DirectYuvLayerHost& layer = host_layers[layer_count_host++];
    // Scrittura diretta nella memoria host page-locked (cuMemAllocHost)
}
...
check_cuda(cuMemcpyHtoDAsync(
               layer_batch_buffer_[slot], host_layers, bytes, active),
           "cuMemcpyHtoDAsync(layer batch)");
```

**Difetto verificato**:
1. Ci sono solo `kLayerBatchSlots = 2` slot di staging host (`layer_batch_host_[0..1]`) e device (`layer_batch_buffer_[0..1]`).
2. `cuMemcpyHtoDAsync` legge `host_layers` in modo **asincrono** tramite il DMA engine della GPU quando il comando arriva in esecuzione sullo stream `active`.
3. Non esiste alcun `CUevent` associato allo slot `slot` che venga atteso prima che la CPU sovrascriva `layer_batch_host_[slot]` (o prima che `cuMemFreeHost` / `cuMemFree` riallochino il buffer alle righe 42 e 103!). Se la CPU sottomette 3 o più frame in rapida successione senza attendere il completamento GPU del frame $N-2$ (o se due chiamate avvengono su stream diversi), la terza chiamata sovrascrive `layer_batch_host_[0]` mentre il DMA del frame $N-2$ non lo ha ancora letto, causando corruzione silenziosa delle posizioni/opacità dei layer.

---

## 2. Come Risolvere e Bonificare Completamente il Problema

### Passo 1 — Allineare il numero di slot al ring del backend (`kLayerBatchSlots = 6`) e aggiungere `CUevent` per slot
In `src/media/video/compositor/cuda/cuda_nv12_compositor_detail.hpp` (e `include/chronon3d/media/video/compositor/cuda/cuda_nv12_compositor.hpp`):
1. Aumentare `kLayerBatchSlots` da `2` a `6`.
2. Aggiungere due array membro:
   ```cpp
   std::array<CUevent, kLayerBatchSlots> layer_batch_ready_events_{};
   std::array<bool, kLayerBatchSlots> layer_batch_slot_in_flight_{};
   ```
3. Nel costruttore di `CudaNv12Compositor`, creare i 6 eventi con `cuEventCreate(&layer_batch_ready_events_[i], CU_EVENT_DISABLE_TIMING)` (overhead quasi nullo perché il timing è disabilitato).
4. Nel distruttore `~CudaNv12Compositor()`, distruggere i 6 eventi con `cuEventDestroy`.

### Passo 2 — Sincronizzare lo slot prima di sovrascriverlo o riallocarlo
In `src/media/video/compositor/cuda/cuda_nv12_compositor_batch.cpp`:
1. All'inizio di `composite_direct_nv12_batch()`, subito dopo aver selezionato `const int slot = layer_batch_slot_;`:
   ```cpp
   check_cuda(cuCtxSetCurrent(context_), "cuCtxSetCurrent");
   if (layer_batch_slot_in_flight_[slot]) {
       check_cuda(cuEventSynchronize(layer_batch_ready_events_[slot]),
                  "cuEventSynchronize(layer batch slot)");
       layer_batch_slot_in_flight_[slot] = false;
   }
   ```
2. Subito dopo `cuLaunchKernel(...)` (riga 125):
   ```cpp
   check_cuda(cuEventRecord(layer_batch_ready_events_[slot], active),
              "cuEventRecord(layer batch slot)");
   layer_batch_slot_in_flight_[slot] = true;
   ```
   Registrando l'evento **dopo** `cuLaunchKernel`, si garantisce con un solo evento sia che `cuMemcpyHtoDAsync` abbia finito di leggere `layer_batch_host_[slot]` sia che il kernel abbia finito di leggere `layer_batch_buffer_[slot]` (rendendo 100% sicura anche l'eventuale riallocazione `cuMemFree` a riga 103!).

---

## 3. Demolition Debt

Nessuno.

---

## 4. Verifica e Criteri di Chiusura

1. **Stress Test Asincrono DirectYUV**:
   - Accodare 12 invocazioni consecutive di `composite_direct_nv12_batch` con coordinate layer distinte per ogni frame senza `cuStreamSynchronize` tra un frame e l'altro: verificare che tutti i 12 frame abbiano hash identici a quelli prodotti con sincronizzazione ad ogni frame.
2. **Suite CUDA NV12**:
   - `chronon3d_compositor_tests` e suite CUDA NV12 (6/6) verdi.

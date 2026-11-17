# TICKET-PERF-P4-DIRECT-YUV-AND-LAYER-BATCH-TILE-CULLING-20261117 — P4: Tile Culling cooperativo in shared memory per eliminare il loop $O(P \times L)$ in DirectYUV (`direct_layer_pixel`) e Vulkan (`layer_batch.comp`)

- **Stato:** CLOSED (2026-11-17 — verified in `build/chronon/linux-video-test`)
- **Priorità:** P2 (Scalabilità con il numero di layer/particelle in DirectYUV CUDA e FullGraph Vulkan)
- **Verifica sul codice attuale:** **PROBLEMA VERO (`[CODICE]` verificato)**
- **File coinvolti:**
  - `src/media/video/compositor/cuda/cuda_nv12_composite_kernels.hpp` (righe 406–438)
  - `src/backends/vulkan/shaders/layer_batch.comp` (righe 78–161)
- **Owner:** C3D CUDA / Vulkan Compositor maintainer

---

## 1. Diagnosi Esatta del Problema

Sia nel compositore CUDA DirectYUV sia nel kernel di batching Vulkan, ogni singolo thread (pixel o blocco $2\times 2$) esegue un ciclo lineare su **tutti i layer del batch**, anche quando i layer sono piccoli e lontani dal pixel corrente:

1. **CUDA DirectYUV** (`src/media/video/compositor/cuda/cuda_nv12_composite_kernels.hpp:406-437`):
   ```cuda
   __device__ float4 direct_layer_pixel(const DirectYuvLayer* layers, int count,
                                        int x, int y, int width, int height) {
     float4 accum = make_float4(0, 0, 0, 0);
     for (int i = 0; i < count; ++i) {
       const DirectYuvLayer& layer = layers[i];
       if (x >= layer.dst_x0 && x < layer.dst_x1 &&
           y >= layer.dst_y0 && y < layer.dst_y1) { ... }
   ```
2. **Vulkan Layer Batch** (`src/backends/vulkan/shaders/layer_batch.comp:78-161`):
   ```glsl
   for (int i = 0; i < params.instance_count; ++i) {
       GpuLayerInstance inst = instances[i];
       ...
   ```

Su una composizione 1080p/4K con $L = 32$ layer o $128$ particelle, ogni pixel legge dalla memoria globale/costante tutte le $L$ strutture `DirectYuvLayer` / `GpuLayerInstance` ($O(P \times L)$).

---

## 2. Come Risolvere e Bonificare Completamente il Problema

Invece di pre-calcolare una struttura tile-to-layer sulla CPU (che aggiungerebbe costo CPU e un secondo buffer da trasferire via PCIe ad ogni frame), possiamo eseguire il **culling per-tile direttamente in `shared` memory all'inizio del workgroup `16×16`** (256 thread):

### Passo 1 — Culling cooperativo in `cuda_nv12_composite_kernels.hpp`
Nel kernel `nv12_composite_layer_batch_2x2` / `p010_composite_layer_batch_2x2` (blocco `16×16` thread che copre un tile di `32×32` pixel luma `[tile_x0 .. tile_x1, tile_y0 .. tile_y1]`):
1. Dichiarare in shared memory una lista compatta di indici attivi nel tile:
   ```cuda
   __shared__ int tile_layer_indices[64];
   __shared__ int tile_layer_count;
   ```
2. Per blocchi di 64 layer (o direttamente se `count <= 64`), il thread `tid = threadIdx.y * 16 + threadIdx.x` (o `tid == 0` se `count <= 16` per preservare l'ordine stretto senza sort, oppure usando una bitmask `__shared__ unsigned long long tile_mask;` aggiornata con `atomicOr(&tile_mask, 1ULL << tid)` dai primi `min(count, 64)` thread!) testa se il bounding box del layer `tid` (incluso l'eventuale `shadow_dx/dy/blur`) interseca `[tile_x0, tile_x1) × [tile_y0, tile_y1)`.
3. Dopo `__syncthreads()`:
   - Se `tile_mask == 0ULL`, l'intero blocco `32×32` non è toccato da nessun overlay: copia direttamente `bg_y` / `bg_uv` in `out_y` / `out_uv` e termina in **O(1)**!
   - Altrimenti, itera solo sui bit attivi di `tile_mask` in ordine crescente (`while (mask) { int i = __ffsll(mask) - 1; mask &= (mask - 1); ... }`), visitando **esclusivamente** i $K \ll L$ layer presenti in quel tile `32×32` e preservando l'ordine del pittore!

### Passo 2 — Applicare lo stesso schema a `src/backends/vulkan/shaders/layer_batch.comp`
1. In `layer_batch.comp` (workgroup `16×16`), processare `params.instance_count` a chunk di 32 istanze usando `shared uint tile_mask;`.
2. Per ogni chunk di 32 istanze:
   - I primi 32 thread del workgroup calcolano il bounding box 2D dell'istanza `base + local_idx` e se interseca il tile `[tile_min .. tile_max]` eseguono `atomicOr(tile_mask, 1u << local_idx)`.
   - `barrier();`
   - Se `tile_mask != 0u`, i pixel del tile iterano solo sui bit attivi con `int bit = findLSB(mask); mask &= mask - 1u;`.

---

## 3. Demolition Debt

Nessuno: algoritmo interamente confinato ai due kernel, zero cambi di ABI o strutture host.

---

## 4. Verifica e Criteri di Chiusura

1. **Parità Pixel Bit-Exact**:
   - L'ordine di iterazione tramite bitmask (`findLSB` / `__ffsll`) è strettamente monotono crescente ($i_1 < i_2 < \dots$), quindi il blending produce pixel **100% bit-exact** rispetto al loop `for (int i = 0; i < count; ++i)`.
2. **Benchmark 1, 4, 16, 32 layer**:
   - Verifica su `chronon3d_compositor_tests` e benchmark `nv12-compositor` con NVENC `p2`.

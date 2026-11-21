# TICKET-PERF-INLINE-MESH-STATIC-BUFFERS-AND-BINNING-20261117 — Separazione buffer statici/dinamici per mesh inline e superamento della scansione $O(\text{pixel} \times \text{triangoli})$

- **Stato:** CLOSED (2026-11-17)
- **Priorità:** P2 (Scalabilità 3D per modelli glTF > 500 triangoli — allineato a `CHRONON_PLAN.md` P2-01, P2-02, P2-06)
- **Ambito:** `src/backends/vulkan/vulkan_backend_inline_mesh_private.cpp`, `src/backends/vulkan/shaders/inline_mesh.comp`, `src/render_graph/nodes/source_node_native_mesh.cpp`
- **Owner:** C3D 3D/Vulkan maintainer
- **Allineamento piano:** `docs/CHRONON_PLAN.md` P2-01, P2-02, P2-06

---

## 1. Problema e Evidenza nel Codice

Il percorso attuale per il rendering Vulkan di mesh 3D (`draw_inline_mesh` in `src/backends/vulkan/vulkan_backend_inline_mesh_private.cpp:28-219` e `src/render_graph/nodes/source_node_native_mesh.cpp`) presenta due colli di bottiglia algoritmici quando il numero di vertici/triangoli cresce (es. personaggi glTF come `low-poly_man` da 5.088 vertici):

1. **Ricalcolo CPU e ri-upload `std::memcpy` dell'intera geometria + indici ad ogni frame** (`vulkan_backend_inline_mesh_private.cpp:103-114`):
   ```cpp
   const std::size_t buffer_slot = 1U + frame_batch.next_slot *
       kPathGeometryPassesPerSlot + frame_batch.pass_count;
   ensure_path_geometry_buffer(bytes, buffer_slot);
   auto& geometry = path_geometry_buffers[buffer_slot];
   std::memcpy(geometry.mapped, projected_lit_vertices.data(),
               projected_lit_vertices.size_bytes());
   auto* packed_indices = static_cast<std::byte*>(geometry.mapped) +
       projected_lit_vertices.size_bytes();
   std::memcpy(packed_indices, indices.data(), indices.size_bytes());
   ```
   Anche quando una mesh ha topologia (`indices`), coordinate UV e normali statiche (cambia solo la posa camera tra il frame $t$ e il frame $t+1$), la CPU:
   - proietta ogni singolo vertice in screen-space;
   - costruisce il buffer interlacciato a 11 float per vertice (`kInlineMeshVertexStride = 11`);
   - copia via `std::memcpy` tutti i vertici e **tutti gli indici** nel ring buffer `path_geometry_buffers[buffer_slot]` a ogni singolo frame;
   - aggiorna un descriptor set (`vkUpdateDescriptorSets` a riga 141) per ogni draw.
2. **Complessità GPU $O(\text{pixel\_bbox} \times \text{triangoli})$** (`inline_mesh.comp:189`):
   - Ogni thread del workgroup `16×16` nel rettangolo `[x0..x1, y0..y1]` legge dal buffer SSBO **tutti i triangoli della mesh** (`i = 0 .. index_count`).
   - Per una mesh con $T = 5.000$ triangoli su un bounding box di $800 \times 800$ pixel, ogni frame esegue $3,2 \times 10^9$ iterazioni del loop.

---

## 2. Come Chiudere Completamente il Ticket (Step-by-Step)

Il lavoro è strutturato in **3 fasi incrementali e indipendenti**, come richiesto da `CHRONON_PLAN.md` P2-01 / P2-02 / P2-06:

### Fase A — Benchmark e Soglia di Scelta (P2-01)
1. Eseguire il benchmark controllato di `inline_mesh.comp` a **10, 100, 1.000 e 10.000 triangoli** (a parità di bounding box coperto, es. `512×512` e `1080p`), registrando i tempi GPU tramite i timestamp per-pass (`completed_frame_timings`).
2. Individuare il punto di crossover $T_{\text{threshold}}$ oltre il quale la scansione lineare per-pixel supera il budget (es. > 0,5 ms per pass).

### Fase B — Separazione Geometria Statica vs Parametri Per-Frame (P2-06)
Per le mesh non deformate da skinning/morph CPU (o separando gli attributi invarianti):
1. **Invarianti per-job**: gli indici (`uint32_t`), le UV (`vec2`) e, per mesh rigide, le posizioni/normali local-space non cambiano mai tra i frame.
2. Usare la `GpuAssetCache` (famiglia canonica `ContentCache`, **senza creare una nuova cache!**) o gli slot persistenti per caricare una sola volta gli indici e gli attributi statici in VRAM, indicizzati dal digest della mesh preparata (`MeshPreparationCache` / `PreparedAssetDigestCache`).
3. Per ogni frame, inviare solo i dati dinamici (matrice MVP / normal matrix nei push constant o le sole posizioni trasformate per mesh skinned), azzerando `gpu_upload_bytes` in steady-state per le mesh statiche animate da camera/transform.

### Fase C — Tile Binning Compute o Pipeline Graphics per Mesh > $T_{\text{threshold}}$ (P2-02)
Due opzioni architetturali ammesse dal piano (scegliere in base a P2-01/P2-02):
- **Opzione C1 (Compute Tile Binning — stesso modello di `text_tile_bin.comp` + `text_tile_raster.comp`)**:
  - Un primo kernel leggero (`inline_mesh_bin.comp`, 1 thread per triangolo) calcola il bounding box 2D del triangolo proiettato e accoda l'indice del triangolo nei bin delle tile `16×16` che interseca.
  - `inline_mesh.comp` itera **solo** sulla lista dei triangoli del proprio tile (`tile_triangle_count` tipicamente 1–15 triangoli invece di 5.000), abbattendo la complessità da $O(P \times T)$ a $O(P \times \bar{k}_{\text{tile}})$ senza introdurre `VkRenderPass` graphics.
- **Opzione C2 (Vulkan Graphics Raster Path — P2-02)**:
  - Pipeline graphics (`vkCmdBeginRendering` / `VkRenderPass`) con vertex/fragment shader che produce esattamente lo stesso output premoltiplicato lineare e lo stesso depth companion `(nearest_depth, depth_marker)`.

---

## 3. Demolition Debt

Se viene mantenuto il path compute brute-force per micro-mesh ($T < T_{\text{threshold}}$, es. quad/`plane3d`) accanto al path tile-binned o graphics:
1. **Owner**: C3D 3D/Vulkan maintainer.
2. **Reason**: Evitare il lancio del pass di binning per geometrie banali (2–32 triangoli come `plane3d` o icone estrusioni semplici).
3. **Exit condition**: Se il pass unificato ha overhead misurato $\le 0,02\text{ ms}$ anche a 2 triangoli, rimuovere il branch non-binned.
4. **Equivalence test/gate**: Parità pixel raw (`MAE = 0` su interi o entro la tolleranza dichiarata) su P0-04, P0-05, P0-06, P0-07, E1, E3.
5. **Removal scope**: Path brute-force in `inline_mesh.comp` / `vulkan_backend_inline_mesh_private.cpp`.
6. **Status**: `ACTIVE` (da aprire all'introduzione di Fase C).

---

## 4. Verifica e Criteri di Accettazione (Definition of Done)

1. **Nessuna nuova Cache/Registry Authority**:
   - `python3 tools/check_architecture.py` deve passare 218/218 (`cache_family_inventory` invariata).
2. **Zero upload indici in steady-state per mesh rigide**:
   - Sul benchmark camera orbit con mesh statica, gli indici e le UV non vengono ricopiati ad ogni frame.
3. **Parità Visiva e Occlusione**:
   - Tutti i 24 ordini di profondità di P0-06 (`evidence/p006-depth-order-vulkan-20261104.md`), i casi alpha-cutout/blend di P0-07 e il caso PBR E1 passano senza regressioni.
4. **Grafico Throughput P2-01/02**:
   - Report in `docs/evidence/` con i millisecondi GPU a 10 / 100 / 1k / 10k triangoli prima e dopo.

---

## 5. Risoluzione Implementata (2026-11-17)

- **Fase A — Benchmark & Crossover**:
  - Misurato su NVIDIA RTX A4000 (`512×512`) prima vs dopo (`docs/evidence/inline-mesh-binning-benchmark-20261117.md`):
    - `10` tris: `0.0375 ms` → `0.0259 ms` (`1.45×`)
    - `100` tris: `0.3095 ms` → `0.0258 ms` (`12.00×`)
    - `1,000` tris: `3.0012 ms` → `0.0432 ms` (`69.47×`)
    - `10,000` tris: `32.3706 ms` → `0.3278 ms` (`98.75×`)
- **Fase B — Separazione Indici Statici**:
  - `vulkan_backend_inline_mesh_private.cpp`: cache LRU a 16 slot (`inline_mesh_static_indices`) indicizzata per hash FNV-1a 64-bit e dimensione byte degli indici; in steady-state `draw_inline_mesh` carica solo `projected_lit_vertices` senza ri-copiare gli indici (zero nuove cache authority).
- **Fase C — Compute Tile Binning (`16×16`)**:
  - `shaders/inline_mesh_bin.comp` (`GpuKernelId::InlineMeshBin`) + `shaders/inline_mesh.comp` con specialization constants `kSpecUseTileBins` e `kSpecSeparateIndexBuffer` (`GpuKernelId::InlineMesh` per `<= 64` tris, `GpuKernelId::InlineMeshBinned` per `> 64` tris, con fallback automatico per tile in overflow `> 256` tris).
- **Verifica**:
  - `chronon3d_backend_registry_tests --test-case="*inline mesh*,*InlineMesh*,*native occlusion*"`: `5/5` test cases, `215/215` assertions PASS.
  - `python3 tools/check_architecture.py`: `218/218` PASS (`cache_family_inventory` invariata).

# TICKET-PERF-VRAM-BANDWIDTH-DEPTH-RG32F-20261117 — Riduzione banda VRAM della companion surface di profondità (`RGBA32F` → `RG32F`) e valutazione hardware sampler

- **Stato:** CLOSED (2026-11-17)
- **Priorità:** P2 (Ottimizzazione Memory Bandwidth GPU e pressione VRAM a 1080p/4K)
- **Ambito:** `src/backends/vulkan/shaders/inline_mesh.comp`, `src/backends/vulkan/`, `src/render_graph/nodes/`
- **Owner:** C3D Vulkan backend / RenderGraph maintainer
- **Allineamento piano:** `docs/CHRONON_PLAN.md` Stage 7 / P2-09 / `TICKET-RENDER-STACK-HARDENING-20261106` W7

---

## 1. Problema e Evidenza nel Codice

1. **Superficie companion `depth_image` sovradimensionata in `RGBA32F` (128 bit/pixel)**:
   - In `src/backends/vulkan/shaders/inline_mesh.comp:17` e `:254-260`:
     ```glsl
     layout(binding = 2, rgba32f) uniform image2D depth_image;
     ...
     vec2 current = imageLoad(depth_image, pixel).rg;
     if (!(current.g > 0.0 && current.r > 0.0 && current.r <= nearest_depth)) {
         imageStore(depth_image, pixel,
                    vec4(nearest_depth,
                    nearest_occludes ? params.depth_marker : -0.5,
                         0.0, 1.0));
     }
     ```
   - La superficie di profondità nativa memorizza **esclusivamente 2 float per pixel**:
     - `.r`: `nearest_depth` (profondità camera-space positiva);
     - `.g`: `depth_marker` (copertura/identità layer: $\ge 0$ per occluder opaco/cutout, $-0.5$ per frammento blend, $-1.0$ per assenza di copertura).
   - I canali `.b` e `.a` sono sempre costanti (`0.0, 1.0`) e non vengono mai letti da nessun consumatore (`inline_mesh.comp`, compositing depth-order P0-06, `PerPixelDofNode` / `publish_dof_provenance_native`).
   - Usare `RGBA32F` (16 byte/pixel) invece di `RG32F` (8 byte/pixel) **spreca esattamente il 50% della banda passante di memoria GPU e il 50% della VRAM** su ogni lettura/scrittura della depth surface (a 1080p: 33,17 MB anziché 16,58 MB; a 4K: 132,71 MB anziché 66,35 MB per superficie).

2. **Campionamento bilineare manuale con 4 `imageLoad` su `image2D` in `VK_IMAGE_LAYOUT_GENERAL`**:
   - In `src/backends/vulkan/shaders/inline_mesh.comp:98-132` (`sample_texture` e `sample_metallic_roughness`), ogni sample bilineare di una texture asset immutabile esegue 4 `imageLoad` separati su una storage image `rgba32f` in layout `GENERAL`.
   - Sulle GPU moderne, le letture da storage image in `VK_IMAGE_LAYOUT_GENERAL` non sfruttano le unità di interpolazione bilineare fissa (TMU) né l'organizzazione cache L1 texture ottimizzata per `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`.

---

## 2. Come Chiudere Completamente il Ticket (Step-by-Step)

### Passo 1 — Censimento di tutti i produttori e consumatori della Depth Surface nativa
1. Cercare nel repository tutti gli shader `.comp` e i metodi `VulkanBackend::Impl` che leggono o scrivono la companion surface di profondità (3D plane, `inline_mesh.comp`, 3D text depth, camera compositor depth merge, DoF native depth provenance).
2. Nota importante su `inline_mesh.comp:120-124` (`vulkan_backend_inline_mesh_private.cpp`):
   ```cpp
   // The shader always declares binding 2; when this draw does not publish
   // depth it is bound to the destination (unused, but a valid descriptor).
   const VkDescriptorImageInfo depth_info{
       VK_NULL_HANDLE, write_depth ? depth_image->view : dst_image.view,
       VK_IMAGE_LAYOUT_GENERAL};
   ```
   Se `binding = 2` diventa `rg32f`, non si può più legare `dst_image.view` (`VK_FORMAT_R32G32B32A32_SFLOAT`) come dummy descriptor quando `write_depth == false` (altrimenti il Validation Layer segnala mismatch tra formato della `VkImageView` e formato `rg32f` dichiarato nello SPIR-V).
   - **Soluzione pulita**: mantenere in `VulkanBackend::Impl` una singola dummy `VkImage` `1×1` di formato `VK_FORMAT_R32G32_SFLOAT` (allocata una sola volta al bootstrap del device, 8 byte totali!) da legare a `binding = 2` quando `write_depth == false`.

### Passo 2 — Migrazione a `RG32F` (`VK_FORMAT_R32G32_SFLOAT`)
1. Aggiungere/usare il formato `PixelFormat::RG32F` in `SurfaceDesc` per le superfici di profondità native.
2. Aggiornare le dichiarazioni `layout(binding = ..., rg32f) uniform image2D depth_image;` negli shader coinvolti e ricompilare gli SPIR-V.
3. Verificare con `tests/test_shader_layout_contract.py` e con il Validation Layer Vulkan attivo che non ci siano warning di format mismatch.

### Passo 3 — Valutazione Misurata `sampler2D` per Texture Asset Read-Only (Stage 7)
1. Dopo la chiusura di `TICKET-PERF-INLINE-MESH-DEFERRED-SHADING-20261117`, misurare con i timestamp GPU se il campionamento texture in `inline_mesh.comp` e `transform` rappresenta ancora > 10% del tempo pass.
2. Se confermato dal profilo, valutare il binding come `sampler2D` (`VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`), verificando rigorosamente che la precisione hardware del filtro bilineare rispetti le soglie di parità software/Vulkan fissate per E1 (`MAE` / `p99`). Se il filtro hardware a 8 bit di frazione sub-texel introduce scostamenti fuori tolleranza nei test `BitExact`, mantenere `imageLoad` per i soli casi `BitExact`.

---

## 3. Demolition Debt

Nessuno: la transizione di `depth_image` da `RGBA32F` a `RG32F` sostituisce in-place il formato sovradimensionato senza mantenere due formati paralleli per la stessa superficie.

---

## 4. Verifica e Criteri di Accettazione (Definition of Done)

1. **Zero errori Vulkan Validation Layer**:
   - Eseguire tutti i test `Vulkan*` (inclusi i casi con `write_depth == false` e `write_depth == true`) con validation layer attivo: 0 errori VUID su descriptor image format.
2. **Parità Pixel Assoluta**:
   - Poiché `.b` e `.a` della depth image non venivano mai letti, l'output colore e profondità di P0-05, P0-06, P0-07, P0-10 (DoF C6 150 frame) ed E1 deve essere **100% bit-exact** (`MAE = 0`) rispetto a prima.
3. **Riduzione VRAM misurata**:
   - Documentare la riduzione di byte allocati per superficie di profondità a 1080p (-16,58 MB per surface) e 4K (-66,35 MB per surface) tramite la telemetria VMA.

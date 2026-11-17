# TICKET-PERF-VULKAN-DIRTY-ROI-AND-PUSH-DESCRIPTORS-20261117 — Abilitazione ROI / Dirty Rectangles sui dispatch Vulkan e riduzione del Descriptor Set Churn (`VK_KHR_push_descriptor`)

- **Stato:** CLOSED (2026-11-17)
- **Priorità:** P2 (Ottimizzazione strutturale banda VRAM su composizioni sparse e riduzione chiamate driver per-pass)
- **Verifica sul codice attuale:** **PROBLEMA VERO (`[CODICE]` verificato)**
- **Ambito:** `src/render_graph/pipeline/tile_execution_policy.cpp`, `src/backends/vulkan/vulkan_descriptor_authority.hpp`, `src/backends/vulkan/vulkan_descriptor_arena_private.cpp`
- **Owner:** C3D Vulkan backend / RenderGraph compiler maintainer
- **Allineamento piano:** `docs/CHRONON_PLAN.md` Stage 8 (`FrameDelta` / `Sparse changed regions/tiles`) e P2-09

---

## 1. Verifica di Veridicità e Evidenza nel Codice

### 1.1 Bypass completo di `FrameDelta` / Dirty Regions quando il backend supporta Native Surfaces
In `src/render_graph/pipeline/tile_execution_policy.cpp:221-225`:
```cpp
const auto* backend = sw_renderer ? sw_renderer->execution_context().backend : nullptr;
if (backend && backend->supports_native_surfaces()) {
    set_full_rgb(plan, "native_surfaces_require_full_rgb");
    return;
}
```
Come spiega il commento alle righe 216-220, il vecchio esecutore `SparseTiles` era pensato per copiare i pixel sulla CPU subito dopo ogni tile, cosa incompatibile con le superfici GPU native.
Tuttavia, forzando `set_full_rgb` indiscriminatamente, anche i pass Vulkan compute che operano su un singolo layer piccolo (es. un badge o sottotitolo `400×80` su una tela `1920×1080` o `3840×2160`) lanciano workgroup sull'intero frame (`1920×1080`) in molti shader (`composite`, `blur`, `color_matrix`, `transform`), anziché restringere il `vkCmdDispatch` al bounding box attivo del layer/ROI (come fanno già con successo `inline_mesh.comp`, `layer_batch.comp` e `dof_blur.comp`!).

### 1.2 Allocazione e scrittura di un nuovo `VkDescriptorSet` per ogni singolo dispatch (`vkAllocateDescriptorSets` + `vkUpdateDescriptorSets`)
In `src/backends/vulkan/vulkan_descriptor_arena_private.cpp` e nei file `vulkan_backend_*_private.cpp` (es. `vulkan_backend_inline_mesh_private.cpp:115-141`):
- Ogni singolo pass compute alloca un `VkDescriptorSet` dal pool del frame slot (`allocate_*_descriptor_set()`) e invoca `vkUpdateDescriptorSets(device, count, writes, 0, nullptr)` prima di `vkCmdBindDescriptorSets`.
- In una scena con 30–80 pass per frame, questo comporta 60–160 chiamate API driver (`vkAllocateDescriptorSets` + `vkUpdateDescriptorSets` + `vkCmdBindDescriptorSets`) per frame.

---

## 2. Come Chiudere Completamente il Ticket (Step-by-Step)

### Parte 1 — Scissor / ROI Push-Constant Dispatch per Pass Compute Vulkan (Senza CPU Tile Readback!)
1. **Non attivare il loop CPU `SparseTiles` su Vulkan**: mantenere un singolo pass GPU per nodo, ma propagare il `predicted_bbox` (o l'unione di `dst_bbox` $\cup$ `prev_bbox` fornita da `FrameDeltaCompiler`) fino al metodo del backend Vulkan.
2. Nei kernel compute full-canvas che ancora dispatchano su `(width + 15) / 16, (height + 15) / 16` (in particolare `blur.comp`, `color_matrix.comp`, `composite`), aggiungere `ivec2 origin; ivec2 end;` nei push constant (esattamente come già implementato in `layer_batch.comp:34-35` e `dof_blur.comp:148-149`) e dispatchare solo:
   ```cpp
   vkCmdDispatch(command,
       (roi_width + 15U) / 16U,
       (roi_height + 15U) / 16U, 1);
   ```
3. Quando il nodo scrive in una superficie precedentemente azzerata (`content_cleared`), non serve nemmeno toccare i pixel esterni alla ROI!

### Parte 2 — Valutazione e Adozione di `VK_KHR_push_descriptor` dentro `VulkanDescriptorAuthority`
1. **Verifica requisito architetturale (`AGENTS.md` / `CHRONON_PLAN.md`)**:
   - **Nessuna seconda authority di descriptor**: la logica deve risiedere interamente dentro l'unica authority canonica `VulkanDescriptorAuthority` (`src/backends/vulkan/vulkan_descriptor_authority.hpp`).
2. **Meccanismo**:
   - All'avvio del device in `vulkan_backend_lifecycle.cpp`, verificare se l'estensione `VK_KHR_push_descriptor` è supportata dal physical device (presente su tutti i driver NVIDIA, AMD e Mesa/llvmpipe moderni) e risolvere una sola volta `pfn_cmd_push_descriptor_set_khr`.
   - Creare i `VkDescriptorSetLayout` con `VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR` quando supportato, oppure fornire un helper unico `VulkanDescriptorAuthority::bind_or_push_descriptors(VkCommandBuffer cmd, VkPipelineLayout layout, std::span<const VkWriteDescriptorSet> writes)` che:
     - Se `VK_KHR_push_descriptor` è attivo: chiama direttamente `pfn_cmd_push_descriptor_set_khr(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, writes.size(), writes.data())` (**1 sola chiamata command-buffer, zero `vkAllocateDescriptorSets`, zero `vkUpdateDescriptorSets`**!).
     - Se non è attivo: usa il fallback esistente `allocate` + `vkUpdateDescriptorSets` + `vkCmdBindDescriptorSets`.

---

## 3. Demolition Debt

Se viene mantenuto il fallback `vkAllocateDescriptorSets` accanto a `VK_KHR_push_descriptor`:
1. **Owner**: C3D Vulkan backend owner.
2. **Reason**: Eventuale compatibilità con ICD Vulkan 1.2 privi di `VK_KHR_push_descriptor` (o qualora `VK_KHR_push_descriptor` venga promosso a requisito obbligatorio di device come `synchronization2`).
3. **Exit condition**: Verifica che sia la GPU di produzione (NVIDIA RTX A4000) sia il software ICD CI (`llvmpipe` / Lavapipe) espongano `VK_KHR_push_descriptor`. Se entrambi lo supportano, promuovere `VK_KHR_push_descriptor` a requisito obbligatorio e rimuovere il pool di allocazione per-pass.
4. **Equivalence test/gate**: `chronon3d_backend_registry_tests` e `chronon3d_effects_tests` 100% PASS.
5. **Removal scope**: `FrameDescriptorAllocator` per-pass in `vulkan_descriptor_arena_private.cpp`.
6. **Status**: `ACTIVE`.

---

## 4. Verifica e Criteri di Accettazione (Definition of Done)

1. **Architecture Gate (`tools/check_architecture.py`)**:
   - La regola `vulkan_descriptor_ownership_is_arena_only` deve restare **PASS** (218/218).
2. **Parità Pixel Assoluta**:
   - Tutte le suite Vulkan (`chronon3d_backend_registry_tests`, `chronon3d_effects_tests`, `chronon3d_compositor_tests`, `chronon3d_render_graph_tests`) devono produrre output bit-exact (`MAE = 0`).
3. **Misura Guadagno**:
   - Riduzione misurata di `gpu_submit_cpu_us` / tempo di registrazione command buffer e riduzione proporzionale del tempo GPU sui pass ROI-clipped.

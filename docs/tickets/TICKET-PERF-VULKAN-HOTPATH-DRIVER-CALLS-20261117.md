# TICKET-PERF-VULKAN-HOTPATH-DRIVER-CALLS-20261117 — Eliminazione chiamate driver `vkGetDeviceProcAddr` e `vkGetPhysicalDeviceProperties` dal hot path per-frame

- **Stato:** CLOSED (2026-11-17 — verified in `build/chronon/linux-video-test`)
- **Priorità:** P2 (Quick Win CPU / Driver Overhead — Impatto immediato, rischio minimo)
- **Ambito:** `src/backends/vulkan/` (`VulkanBackend::Impl`)
- **Owner:** C3D Vulkan backend maintainer
- **Allineamento piano:** `docs/CHRONON_PLAN.md` Stage 3 (Kill fixed CPU overhead) / P2-03

---

## 1. Problema e Evidenza nel Codice

Durante l'esecuzione di ogni singolo frame sul backend Vulkan, vengono effettuate chiamate dirette al loader/driver Vulkan per interrogare puntatori a funzione e proprietà hardware statiche del device che non possono mai cambiare dopo `vkCreateDevice`:

1. **Lookup `vkGetDeviceProcAddr` ad ogni barriera emessa** (`src/backends/vulkan/vulkan_backend_sync_private.cpp:13-21`):
   ```cpp
   PFN_vkCmdPipelineBarrier2KHR require_pipeline_barrier2(VkDevice device) {
       const auto pipeline_barrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(
           vkGetDeviceProcAddr(device, "vkCmdPipelineBarrier2KHR"));
       if (!pipeline_barrier2) {
           throw std::runtime_error(
               "Vulkan: vkCmdPipelineBarrier2KHR unavailable after synchronization2 enablement");
       }
       return pipeline_barrier2;
   }
   ```
   Questa funzione viene chiamata dentro:
   - `VulkanBackend::Impl::emit_resource_transition()` (`vulkan_backend_sync_private.cpp:293`)
   - `VulkanBackend::Impl::emit_buffer_barriers2()` (`vulkan_backend_sync_private.cpp:408`)
   - `VulkanBackend::Impl::emit_cuda_surface_ownership_transfer()` (`vulkan_backend_sync_private.cpp:473`)
   - `VulkanBackend::synchronize_native_dependencies()` (`vulkan_backend_sync_private.cpp:546`)
   In un frame tipico con 20–50 pass e relative transizioni di stato, il motore esegue decine di lookup di stringa `"vkCmdPipelineBarrier2KHR"` nella dispatch table del driver per ogni frame.
   Al contrario, per i calibrated timestamps (`pfn_get_calibrated_timestamps` in `src/backends/vulkan/vulkan_backend_impl.hpp:163`), il puntatore è già correttamente risolto e cachato una sola volta in `VulkanBackend::Impl`.

2. **Query `vkGetPhysicalDeviceProperties` ad ogni draw call di mesh inline** (`src/backends/vulkan/vulkan_backend_inline_mesh_private.cpp:96-99`):
   ```cpp
   VkPhysicalDeviceProperties properties{};
   vkGetPhysicalDeviceProperties(physical_device, &properties);
   if (bytes > properties.limits.maxStorageBufferRange)
       throw std::length_error("Vulkan inline mesh exceeds maxStorageBufferRange");
   ```
   Ogni invocazione di `VulkanBackend::Impl::draw_inline_mesh()` copia l'intera struct `VkPhysicalDeviceProperties` (oltre 800 byte, inclusi array di limiti e stringhe del device) dal driver solo per leggere la costante `limits.maxStorageBufferRange`.

---

## 2. Come Chiudere Completamente il Ticket (Step-by-Step)

### Passo 1 — Aggiungere i campi cachati in `VulkanBackend::Impl`
In `src/backends/vulkan/vulkan_backend_impl.hpp` (accanto a `pfn_get_calibrated_timestamps` alla riga 163):
- Aggiungere:
  ```cpp
  PFN_vkCmdPipelineBarrier2KHR pfn_cmd_pipeline_barrier2{nullptr};
  VkDeviceSize max_storage_buffer_range{0};
  ```

### Passo 2 — Inizializzare i campi una sola volta al bootstrap del device
In `src/backends/vulkan/vulkan_backend_lifecycle.cpp` (subito dopo la creazione di `device` e la verifica del supporto `synchronization2`):
1. Risolvere `pfn_cmd_pipeline_barrier2`:
   ```cpp
   m_impl->pfn_cmd_pipeline_barrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(
       vkGetDeviceProcAddr(m_impl->device, "vkCmdPipelineBarrier2KHR"));
   if (!m_impl->pfn_cmd_pipeline_barrier2) {
       throw std::runtime_error(
           "Vulkan: vkCmdPipelineBarrier2KHR unavailable after synchronization2 enablement");
   }
   ```
2. Salvare `max_storage_buffer_range` dalle proprietà già lette in fase di init del physical device:
   ```cpp
   VkPhysicalDeviceProperties props{};
   vkGetPhysicalDeviceProperties(m_impl->physical_device, &props);
   m_impl->max_storage_buffer_range = props.limits.maxStorageBufferRange;
   ```
3. Azzerare `pfn_cmd_pipeline_barrier2 = nullptr` e `max_storage_buffer_range = 0` nel teardown di `VulkanBackend::Impl`.

### Passo 3 — Sostituire i call site nel hot path
1. In `src/backends/vulkan/vulkan_backend_sync_private.cpp`:
   - Rimuovere `require_pipeline_barrier2(VkDevice)` dall'anonymous namespace (o trasformarla in un membro helper `Impl::cmd_pipeline_barrier2()` che controlla `if (!pfn_cmd_pipeline_barrier2) throw ...; return pfn_cmd_pipeline_barrier2;`).
   - Sostituire le 4 chiamate alle righe 293, 408, 473, 546 con il puntatore cachato.
2. In `src/backends/vulkan/vulkan_backend_inline_mesh_private.cpp:96-99`:
   - Sostituire `vkGetPhysicalDeviceProperties` con:
     ```cpp
     if (bytes > max_storage_buffer_range)
         throw std::length_error("Vulkan inline mesh exceeds maxStorageBufferRange");
     ```

---

## 3. Demolition Debt

Nessun bridge o doppia via introdotta: la modifica rimuove direttamente le chiamate driver ridondanti dal hot path.

---

## 4. Verifica e Criteri di Accettazione (Definition of Done)

1. **Zero chiamate nel hot path**:
   - `git grep vkGetDeviceProcAddr src/backends/vulkan/` deve mostrare occorrenze **solo** in `vulkan_backend_lifecycle.cpp` (o debug context init), mai in `*_sync_private.cpp` o file di esecuzione pass.
   - `git grep vkGetPhysicalDeviceProperties src/backends/vulkan/` non deve mostrare alcuna occorrenza in `vulkan_backend_inline_mesh_private.cpp`.
2. **Build e Staleness Check**:
   - Compilare con preset `linux-fast-dev` (`build/fast`).
   - Verificare che i binari di test siano più recenti dei sorgenti modificati prima di invocare `ctest`.
3. **Suite di test obbligatorie verdi**:
   - `chronon3d_backend_registry_tests`
   - `chronon3d_compositor_tests`
   - `chronon3d_render_job_contract_tests`
   - `chronon3d_render_graph_tests`
   - Developer gates (`tools/check_architecture.py` e i 6 gate rapidi di `tools/gates/manifest.sh`).
4. **Parità Pixel**:
   - Bit-exact (`MAE = 0`, hash invariati) rispetto alla baseline pre-modifica.

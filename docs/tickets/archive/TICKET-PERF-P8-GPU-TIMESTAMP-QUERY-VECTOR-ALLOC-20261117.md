# TICKET-PERF-P8-GPU-TIMESTAMP-QUERY-VECTOR-ALLOC-20261117 — P8: Eliminazione dell'allocazione heap `std::vector<std::uint64_t>` in `read_gpu_timestamps()`

- **Stato:** CLOSED (2026-11-17 — verified in `build/chronon/linux-video-test`)
- **Priorità:** P2 (Quick Win: eliminazione allocazione heap nel percorso caldo di retirement dei frame Vulkan)
- **Verifica sul codice attuale:** **PROBLEMA VERO (`[CODICE]` verificato)**
- **File coinvolti:**
  - `src/backends/vulkan/vulkan_kernel_replay_private.cpp` (righe 73–84)
- **Owner:** C3D Vulkan backend maintainer

---

## 1. Diagnosi Esatta del Problema

In `src/backends/vulkan/vulkan_kernel_replay_private.cpp:73-80`, dentro `VulkanBackend::Impl::read_gpu_timestamps(std::size_t slot)`:

```cpp
if (pass_count != 0) {
    std::vector<std::uint64_t> stamps(2 * pass_count, 0);
    const auto query_count = static_cast<std::uint32_t>(2 * pass_count);
    const VkResult result = vkGetQueryPoolResults(
        device, timestamp_pool, slot_base + 2, query_count,
        stamps.size() * sizeof(std::uint64_t), stamps.data(),
        sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);
```

**Difetto verificato**:
`read_gpu_timestamps(slot)` viene invocata ad ogni ritiro di uno slot del `FrameBatchState` (cioè ad ogni frame renderizzato sulla GPU).
Poiché `pass_count` è già limitato superiormente per costruzione a `FrameBatchState::kCompiledPassTimingCapacity` (vedi `src/backends/vulkan/vulkan_backend_sync_private.cpp:359`), allocare e deallocare ogni volta un `std::vector<std::uint64_t>` sull'heap introduce una coppia `malloc`/`free` (e azzeramento memoria) ad ogni frame dentro il codice stesso della telemetria GPU.

---

## 2. Come Risolvere e Bonificare Completamente il Problema

### Passo Unico — Usare un `std::array` stack-allocated di dimensione `2 * FrameBatchState::kCompiledPassTimingCapacity`
In `src/backends/vulkan/vulkan_kernel_replay_private.cpp:73-84`:

```cpp
if (pass_count != 0) {
    const std::size_t clamped_passes = std::min<std::size_t>(
        pass_count, FrameBatchState::kCompiledPassTimingCapacity);
    std::array<std::uint64_t, 2 * FrameBatchState::kCompiledPassTimingCapacity> stamps{};
    const auto query_count = static_cast<std::uint32_t>(2 * clamped_passes);
    const VkResult result = vkGetQueryPoolResults(
        device, timestamp_pool, slot_base + 2, query_count,
        query_count * sizeof(std::uint64_t), stamps.data(),
        sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);
```
E usare `clamped_passes` come limite del ciclo `for (std::size_t pass = 0; pass < clamped_passes; ++pass)` alla riga 89.

---

## 3. Demolition Debt

Nessuno.

---

## 4. Verifica e Criteri di Chiusura

1. **Zero Allocazioni in `read_gpu_timestamps`**:
   - Nessun `std::vector` presente in `read_gpu_timestamps()`.
2. **Suite di Test**:
   - `chronon3d_backend_registry_tests` e `chronon3d_render_job_contract_tests` verdi.

# TICKET-VULKAN-TIMING-V1 — Vulkan CPU vs GPU timing + sync/readback metrics

## Stato: OPEN (implementazione su `main`, macchina-verifica DEFERRED-WBH)

## Problema

Il backend Vulkan non distingue il costo CPU (submit/wait/readback) dal costo
GPU (elapsed) né espone il costo della sincronizzazione CPU→GPU. Senza queste
metriche non è possibile rispondere a:

- il frame è lento per il submit CPU o per l'esecuzione GPU?
- quanto tempo la CPU passa bloccata su `vkWaitForFences` (sync point)?
- quanto costa il readback GPU→CPU?

Questo è il prerequisito per giustificare il futuro percorso RGBA GPU → NV12 GPU
→ NVENC senza readback CPU.

## Soluzione (implementata)

### CPU-side (safe)

| Counter | Punto di cablaggio |
| ------- | ------------------ |
| `gpu_submit_cpu_us` | `vkQueueSubmit` in `submit()` e `submit_batch()` |
| `gpu_wait_cpu_us` | `vkWaitForFences` in `wait_for_pending()` (fence immediato + slot frame-batch) |
| `readback_us` | `vkMapMemory`/`memcpy`/`vkUnmapMemory` in `download()` |
| `cpu_gpu_sync_us` | derivato = `gpu_wait_cpu_us + readback_us` (export) |

### GPU-side (timestamp query, frame-level)

`gpu_execute_us` misura l'esecuzione GPU del frame tramite un `VkQueryPool`
`VK_QUERY_TYPE_TIMESTAMP` (2 query per slot del ring `FrameBatchState`):

1. ctor: legge `limits.timestampPeriod` + `limits.timestampValidBits`; crea il
   query pool solo se `timestampValidBits != 0`.
2. `begin_frame_batch()` (full-reset): `vkCmdResetQueryPool` +
   `vkCmdWriteTimestamp(START, TOP_OF_PIPE)` dopo `vkBeginCommandBuffer`.
3. `submit_batch()`: `vkCmdWriteTimestamp(END, BOTTOM_OF_PIPE)` prima di
   `vkEndCommandBuffer`.
4. `read_gpu_timestamps(slot)`: dopo il fence-wait del riuso slot (in
   `begin_frame_batch()` o `wait_for_pending()`), `vkGetQueryPoolResults`
   `(VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT)` e
   `(end - start) * timestampPeriod` → accumulato in `gpu_execute_us`.

Il path command-batch (N overlay in un solo submit) scrive START una volta e END
una volta: il delta copre l'intero batch, che è il comportamento corretto per un
singolo `vkQueueSubmit`.

## Evidenza (file toccati)

- `include/chronon3d/backends/vulkan/vulkan_backend.hpp` — 4 campi in
  `VulkanBackendStats` + docstring export.
- `src/backends/vulkan/vulkan_backend.cpp` — include `profiling.hpp`, campi
  query pool nel `Impl`, creazione/distruzione pool, `read_gpu_timestamps`,
  cablaggio START/END/read + export dei 5 counter (4 raw + 1 derivato).

## Criteri di accettazione

- [ ] I 5 counter compaiono in `export_gpu_telemetry_counters` e persistono in
      `render_counters` (key-value) senza toccare `RenderCounters`/X-macro.
- [ ] `gpu_submit_cpu_us` cresce con ogni `vkQueueSubmit`.
- [ ] `gpu_wait_cpu_us` cresce solo quando la CPU è bloccata su un fence.
- [ ] `readback_us` cresce solo in `download()`.
- [ ] `gpu_execute_us` cresce solo se `timestampValidBits != 0` e il frame è
      stato eseguito (read al riuso slot, mai doppio-read dello stesso slot).
- [ ] Nessun nuovo simbolo SDK pubblico oltre ai campi di `VulkanBackendStats`.

## Forward-points

- **TICKET-VULKAN-TIMING-V1-PASSES** — per-pass GPU timings (`--timing-level
  passes`): `vkCmdWriteTimestamp` attorno a ogni `record_*` (transform, blur,
  composite, color_adjust, text, matte, glow), esportati come
  `pass_gpu_us{name}`. Richiede un pool dimensionato per un massimo di pass per
  frame + mappatura nome→indice query; overhead non trascurabile → opzionale
  dietro flag.
- **TICKET-VULKAN-TIMING-V1-WBH-VERIFY** — build + `ctest` su working build host
  (questo ambiente non dispone di vcpkg + SDK Vulkan); verifica che un render
  fully-pipelined riporti `gpu_wait_cpu_us ≈ 0`, `readback_us > 0` solo in
  presenza di download, e `gpu_execute_us > 0` su device con timestamp support.

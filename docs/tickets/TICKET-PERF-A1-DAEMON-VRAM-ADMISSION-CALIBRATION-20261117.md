# TICKET-PERF-A1-DAEMON-VRAM-ADMISSION-CALIBRATION-20261117 — A1: Sostituzione del floor VRAM statico di `8800 MiB` con stima basata sulla shape del job in `daemon_render_concurrency.hpp`

- **Stato:** CLOSED (2026-11-17)
- **Priorità:** P2 (Sblocco del parallelismo multi-job in-process sul daemon per composizioni FullGraph leggere)
- **Verifica sul codice attuale:** **PROBLEMA VERO (`[CODICE]` verificato)**
- **File coinvolti:**
  - `apps/chronon3d_cli/daemon/daemon_render_concurrency.hpp` (righe 41–54)
- **Owner:** C3D Daemon / Runtime maintainer
- **Allineamento piano:** `docs/CHRONON_PLAN.md` P2-08 (`VRAM/job-class admission and backpressure without spin loops`)

---

## 1. Diagnosi Esatta del Problema

In `apps/chronon3d_cli/daemon/daemon_render_concurrency.hpp:45-46`:

```cpp
inline constexpr std::uint64_t kMeasuredFullGraphPeakBytes =
    8800ULL * 1024ULL * 1024ULL;
```

**Difetto verificato**:
La costante `kMeasuredFullGraphPeakBytes = 8800 MiB` viene usata come costo fisso di ammissione per **qualunque** job FullGraph, perché era stata calibrata storicamente quando ogni frame NVDEC/export tratteneva una superficie 1080p esterna senza mai rilasciarla (leak poi risolto in `src/backends/vulkan/vulkan_backend_submission_private.cpp:142-158`: *"Marking those slots export_handed_off retained one 1080p external image per frame forever"*).
Su una GPU da 16 GB (RTX A4000), addebitare `8.800 MiB` ad ogni job FullGraph impedisce matematicamente (`2 × 8,8 GB = 17,6 GB > 16 GB`) di eseguire anche solo 2 job overlay 1080p leggeri in parallelo, nonostante il loro consumo reale oggi sia < 1,2 GiB ciascuno.

---

## 2. Come Risolvere e Bonificare Completamente il Problema

### Passo 1 — Ri-misurare il Picco VRAM Post-Fix sul Corpus FullGraph 1080p e 4K
1. Eseguire su RTX A4000 la telemetria VMA esistente su:
   - (a) Overlay FullGraph 1080p standard (testo + 2 layer immagine + glow);
   - (b) Scena 3D / DoF C6 1080p (150 frame);
   - (c) Scena 4K FullGraph.
2. Verificare che con il fix di `export_handed_off` (`vulkan_backend_submission_private.cpp:146`), il picco VRAM sia limitato al numero fisso di slot fisici in `CompiledResourceTable` + ring di 6 slot (`FrameBatchState::kSlotCount`).

### Passo 2 — Parametrizzare la Stima VRAM in `daemon_render_concurrency.hpp`
In `apps/chronon3d_cli/daemon/daemon_render_concurrency.hpp`:
1. Calcolare il requisito di ammissione in funzione della risoluzione `(width, height)` e del numero di slot fisici compilati (`CompiledResourceTable::physical_slot_count`):
   $$\text{Bytes}_{\text{job}} = \text{Bytes}_{\text{base}} + (\text{physical\_slots} + 6) \times W \times H \times 16 + \text{Bytes}_{\text{asset\_headroom}}$$
2. Mantenere `kMeasuredFullGraphPeakBytes` solo come cap/fallback per job privi di metadati di compilazione, garantendo tramite il `DeviceScheduler` che la somma dei byte prenotati dai job attivi non superi mai il budget VMA disponibile.

---

## 3. Demolition Debt

Nessuno.

---

## 4. Verifica e Criteri di Chiusura

1. **Test Concorrenza Daemon (P2-08)**:
   - `chronon3d_cli_tests` verde; verificare che 2 job FullGraph 1080p leggeri vengano ammessi senza OOM e che 2 job 4K pesanti che superano il budget vengano correttamente accodati dal `DeviceScheduler`.

---

## 5. Risoluzione Implementata (2026-11-17)

- **Misura VMA post-fix su RTX A4000** (`chronon3d_cli render --plan`):
  - `tools/fixtures/vram-calibration-1080p-overlay.plan.json`: `vma_usage_bytes = 238.3 MiB` (`vma_allocation_count = 14`).
  - `tools/fixtures/vram-calibration-4k-overlay.plan.json`: `vma_usage_bytes = 864.8 MiB` (`vma_allocation_count = 19`).
- **Stima shape-aware**:
  - `apps/chronon3d_cli/daemon/daemon_service_ipc_protocol.hpp/.cpp`: aggiunto `PlanAdmissionShape::estimated_physical_slots` estratto dalla topologia `layers` del piano.
  - `apps/chronon3d_cli/daemon/daemon_render_concurrency.hpp`: aggiunta `estimate_shaped_vram_bytes(width, height, physical_slots, direct_yuv, native_nvenc, asset_headroom_bytes)` (`512 MiB + (physical_slots + 6) * W * H * 16 + headroom`) con fallback su `estimate_peak_vram_bytes` (`kMeasuredFullGraphPeakBytes`) quando `physical_slots == 0`; collegata in `resolve_device_requirements` e `daemon_service_ipc_render_job.cpp`.
- **Verifica**:
  - `chronon3d_cli_tests`: `Passed (95.38 sec)`
  - `chronon3d_daemon_render_concurrency_tests`: `Passed (0.06 sec)` (include test `A1 shaped vram admission`: 2 job 1080p leggeri ammessi in parallelo su `DeviceScheduler`, 2 job 4K pesanti accodati)
  - `python3 tests/test_gpu_lane_profiles.py`: `Ran 3 tests ... OK`

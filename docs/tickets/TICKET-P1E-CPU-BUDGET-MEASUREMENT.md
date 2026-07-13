# TICKET-P1E-CPU-BUDGET-MEASUREMENT — CpuBudget render/encode timing measurement

## Stato: HARNESS-COMPLETE (2026-07-13, macchina-verifica DEFERRED to working build host)

## Contesto

Il piano di semplificazione punto #13 chiede di misurare il budget CPU
prima di qualsiasi cambiamento ai pool. `CpuBudget` ha unificato i thread
di render, decode ed encode e ha rimosso il `WritePool` separato. Questo
rende il ciclo render/write sequenziale.

L'analisi deve determinare se l'encode rappresenta una parte significativa
del tempo totale. Se sì, estendere **lo stesso CpuBudget** con una pipeline
bounded. NON creare WritePoolV2/EncoderPool/AsyncFrameWriter/BackgroundOutputManager
come sistemi separati.

## Architettura attuale (machine-verified)

### CpuBudget (include/chronon3d/core/cpu_budget.hpp)

```cpp
struct CpuBudget {
    int render_threads{0};   // TBB task arena / ExecutionScheduler
    int decode_threads{0};   // future video decoder threads
    int encode_threads{0};   // encoder internal threads (x264) + writer thread
};
```

Presets per machine class (cpu_budget.cpp:54-108):
- Desktop: 75% render, 12.5% decode, 12.5% encode (16 cores → 12/2/2)
- Laptop: 50% render, 25% decode, 25% encode
- Server: 80% render, 10% decode, 10% encode
- Embedded: total-2 render, 1 decode, 1 encode

### Pipeline render/encode — due modalità

**1. Pipe mode** (video_export_pipe.cpp):
- Render loop + writer thread separato
- Encoder riceve frame via pipe ffmpeg
- `render_ms` + `encode_ms` misurati separatamente
- Coda bounded con back-pressure (`queue_wait_ms` tracciato)

**2. Chunked mode** (video_export_chunked.cpp):
- Render parallelo in chunk worker threads (fino a `--chunks` worker)
- Ogni worker: render frame → save PNG → locale
- Encode: ffmpeg post-hoc su tutti i PNG (sequenziale, dopo il render)
- `render_ms` = tempo totale worker, `encode_ms` = tempo ffmpeg

**3. Still/job mode** (render_job_loop.cpp):
- Ciclo sequenziale render/write dopo rimozione WritePool
- Commento esplicito: "The previous WritePool-based double buffering has
  been removed in favour of the unified CpuBudget. Encode work is now
  accounted for in the encode_threads budget and runs synchronously in
  the render thread."

### Cosa è già stato rimosso

- `WritePool` — il double-buffering pool separato è stato eliminato
- Nested `std::thread` pool — rimosso in favore di CpuBudget unificato
- Il ciclo render/write è ora sequenziale nel job mode

### Cosa NON esiste ancora (da NON creare come sistemi separati)

- WritePoolV2
- EncoderPool
- AsyncFrameWriter
- BackgroundOutputManager

## Script di misurazione

`tools/measure_cpu_budget.sh` — HARNESS-COMPLETE:
- Run 1: Still render (1 frame, pure render time, no encode)
- Run 2: Video pipe mode (60 frames, interleaved render+encode)
- Run 3: Video chunked mode (60 frames, separate render then encode)
- Metriche: render_ms, encode_ms, wall_ms, CPU%, peak RSS, frames/min
- Decision rule: se encode_ratio > 25% di (render+encode), estendi CpuBudget
  con bounded pipeline
- Output: `docs/baselines/cpu-budget-<timestamp>.txt`

## §honesty limitation

Macchina-verifica DEFERRED to working build host per:

1. **Build rot preesistente**: `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT` —
   `NodeCacheKey` non riconosciuto in `node_executor.cpp` blocca la
   compilazione di `chronon3d_cli`. Rot CONTAINED su HEAD, non introdotto
   da questo chore.
2. **CMake 3.22 vs CMakePresets v6**: la VPS ha cmake 3.22 (system) che
   non supporta CMakePresets version 6. Cmake 4.4.0 installato via pip
   ma il configure manuale con il path assoluto
   `/home/pierone/vcpkg/scripts/buildsystems/vcpkg.cmake` è andato in
   timeout (vcpkg stava compilando le dipendenze da zero, 300s).
3. **Build dirs esistenti**: `build/chronon/linux-content-dev` (3.2G)
   configurata con CLI ON ma il build fallisce per il rot preesistente.

## Piano di esecuzione (working build host)

```bash
# 1. Risolvi il rot preesistente (TICKET-NODE-CACHE-KEY-COLLAPSE-ROT)
# 2. Build chronon3d_cli
cmake --build build/chronon/linux-content-dev --target chronon3d_cli -j$(nproc)

# 3. Esegui la misurazione
bash tools/measure_cpu_budget.sh

# 4. Analizza i risultati
#    - Se encode_ratio > 25%: estendi CpuBudget con bounded pipeline
#    - Se encode_ratio ≤ 25%: il ciclo sequenziale è accettabile
#    - NON creare pool separati in nessun caso
```

## Criteri di accettazione

- [ ] `tools/measure_cpu_budget.sh` eseguito su working build host
- [ ] Baseline salvata in `docs/baselines/cpu-budget-<timestamp>.txt`
- [ ] Decisione documentata: estendi CpuBudget vs mantieni sequenziale
- [ ] Se estensione: implementata nello stesso CpuBudget (no pool separati)

## Cross-link

- `include/chronon3d/core/cpu_budget.hpp` (CpuBudget struct + presets)
- `src/core/cpu_budget.cpp` (preset logic + env var parsing)
- `apps/chronon3d_cli/utils/job/render_job_loop.cpp` (sequential loop, WritePool removed)
- `apps/chronon3d_cli/commands/video/exporters/video_export_pipe.cpp` (pipe mode)
- `apps/chronon3d_cli/commands/video/exporters/video_export_chunked.cpp` (chunked mode)
- `apps/chronon3d_cli/commands/video/common/pipe_export_helpers.cpp:96` (encode_threads wiring)
- `src/core/scheduler/execution_scheduler.cpp:86` (render_threads → worker_count)
- `tools/measure_cpu_budget.sh` (measurement script)
- `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT` (build rot blocker)
- `TICKET-BUILD-ROT-CASCADE-CAMERA` (409-error build rot)
- `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` (vcpkg env block)

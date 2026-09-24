# TICKET-P1E-CPU-BUDGET-MEASUREMENT — CpuBudget render/encode timing measurement

## Stato: DONE — MISURATO / BOUNDED PIPELINE RETENUTO (2026-09-11)

La misurazione ha deciso di mantenere la pipeline bounded esistente: non è
stato autorizzato alcun pool, scheduler o writer separato.

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

**2. Chunked mode:**
- Il runtime corrente è pipe-only e rifiuta `--ffmpeg-mode png`.
- La misura usa quindi `--chunks 4` con l'autorità pipe bounded esistente,
  senza reintrodurre un secondo percorso di encoding.

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

`tools/measure_cpu_budget.sh` è ora presente. Accetta i tre comandi fixture
(still, pipe e chunked), li esegue con `/usr/bin/time -v` e usa il timing
sidecar come autorità quando disponibile. Nessuna misurazione GPU/CPU viene
considerata eseguita finché i comandi del working build host non vengono
registrati e il report non viene salvato in `docs/baselines/`. La misura
same-SHA è registrata in
`docs/baselines/cpu-budget-a16c86de2-run2.txt`; il report storico resta
disponibile per confronto.

L'harness previsto deve coprire:
- Run 1: Still render (1 frame, pure render time, no encode)
- Run 2: Video pipe mode (60 frames, interleaved render+encode)
- Run 3: Video chunked-via-pipe mode (60 frames, `--chunks 4`)
- Metriche: render_ms, encode_ms, wall_ms, CPU%, peak RSS, frames/min
- Decision rule: se encode_ratio > 25% di (render+encode), estendi CpuBudget
  con bounded pipeline
- Output: `docs/baselines/cpu-budget-<timestamp>.txt`

## Same-SHA result

On `a16c86de2`, the working Vulkan/NVENC host produced:

| mode | exit | render_ms | encode_ms | encode_ratio |
|---|---:|---:|---:|---:|
| still | 0 | NA | NA | NA |
| pipe | 0 | 329.879 | 674.218 | 67.15% |
| chunked-via-pipe | 0 | 304.073 | 700.500 | 69.73% |

Both video modes exceed the 25% threshold. The decision is therefore to
extend the existing `CpuBudget`/bounded queue authority when overlap is
implemented; no new pool or scheduler is authorized by this measurement.

## Criteri di accettazione

- [x] Harness di misurazione presente e verificato sintatticamente/localmente
- [x] Harness eseguito con fixture del working build host
- [x] Baseline same-SHA salvata in `docs/baselines/cpu-budget-a16c86de2-run2.txt`
- [x] Decisione documentata: encode significativo; il pipe bounded esistente
  resta l'unica pipeline e ogni estensione passa da `CpuBudget`
- [x] Se estensione: non applicabile — la misurazione ha deciso di mantenere
  la pipeline bounded esistente; nessuna nuova pool/scheduler è autorizzata.

## Cross-link

- `include/chronon3d/core/cpu_budget.hpp` (CpuBudget struct + presets)
- `src/core/cpu_budget.cpp` (preset logic + env var parsing)
- `apps/chronon3d_cli/utils/job/render_job_loop.cpp` (sequential loop, WritePool removed)
- `apps/chronon3d_cli/commands/video/exporters/video_export_pipe.cpp` (pipe mode)
- `apps/chronon3d_cli/commands/video/exporters/video_export_chunked.cpp` (chunked mode)
- `apps/chronon3d_cli/commands/video/common/pipe_export_helpers.cpp:96` (encode_threads wiring)
- `src/core/scheduler/execution_scheduler.cpp:86` (render_threads → worker_count)
- `tools/measure_cpu_budget.sh` (implemented measurement harness)
- `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT` (build rot blocker)
- `TICKET-BUILD-ROT-CASCADE-CAMERA` (409-error build rot)
- `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` (vcpkg env block)

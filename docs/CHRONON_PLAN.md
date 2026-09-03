# Chronon — Next Steps

> `AGENTS.md` vieta `NEXT_STEPS.md` come nome, questo è il doc operativo sostitutivo (`CHRONON_PLAN.md`).
> Stato corrente: [`CURRENT_STATUS.md`](CURRENT_STATUS.md) — HEAD `86c165cb` (`origin/main`).
> Requisiti release: [`RELEASE_GATE.md`](RELEASE_GATE.md).

## Principio

Chronon è **GPU-first, CPU oracle**. Dopo `M4 Vulkan` e il video compiler pipeline, la direzione è:
native surfaces, zero-copy, zero intermediate copies, zero unnecessary work.

Ogni fase deve chiudere con **stesso-SHA baseline verde**
(`verify_chronon_product_linux.sh` → `CHRONON_PRODUCT_FUNCTIONAL_PASS`), non con harness `WIRED`.

---

## Fase 0 — Certificazione main (1-2 settimane, blocca tutto)

Obiettivo: HEAD verde. Prima di aggiungere qualsiasi feature, certificare cosa esiste realmente.

1. **Inventario del worktree** — Verifica quali feature dichiarate esistono nel codice:

   ✅ **Confermati nel main remoto** (codice + test):
   - `DeviceScheduler` — RAII + pressure-based placement, 3 test in `tests/runtime/test_device_scheduler.cpp`
   - `GpuLayerBatch` — `LayerInstance` + `GpuLayerBatch` + `make_gpu_batch()`, 6 test
   - `ParameterRingWriter` — SoA offset/size, triple-buffered
   - `CommandReplayDescriptor` — header-only bridge, `allocate_slots()`, `write_frame()`
   - `FusedPixelProgram` — ABI surface con 4 guard, 7 test in `test_fusion_pass.cpp`
   - `NativeAvEncoder` — full pipeline: open, write_native_surface, drain_packets, close
   - `SceneExecutionMode` — `StaticScene`, `DynamicCallback`, `StaticTopologySlots`
   - `ColorMetadata` — NV12/P010 multi-format support
   - `MTSDF distance field` — `Mtsdf = 3` (RGBA) in `gpu_glyph_atlas.hpp`, usato negli shader
   - `video_export_chunked` — `avformat_write_header` in `video_export_chunked.cpp:377`

   ⚠️ **Parziali** (esistono ma non completamente cablati):
   - `text_tile_bin` — shader binning esiste, compilato nel build Vulkan, ma non è ancora
     il percorso unico (coesiste con `text_batch.comp` per-pixel)
   - `text_tile_raster` — shader raster tile-based esiste, shared memory + bitonic sort,
     ma non è ancora il drop-in replacement di `text_batch.comp`
   - `text_batch.comp` — ha ancora `for (int i = 0; i < params.glyph_count; ++i)` per pixel.
     La coesistenza con il tile-based path non è risolta.
   - `In-process remux` — `avformat_write_header` esiste in `video_export_chunked.cpp:377`
     ma `In-process remux = 0 ms` NON è dimostrato dal benchmark PNG.

   ❌ **Assenti** (nessun codice nel main remoto):
   - `FrameDelta` / `ExecutionResolver` / `SparseRenderPlan` — 0 risultati
   - `DeviceSessionPool` — 0 risultati
   - `FontClassification` — 0 risultati
   - `CudaYuvLayerBatchExecutor` — 0 risultati
   - `pixelDomain inference` — 0 risultati
   - `SIMO preparation` — 0 risultati (solo design in ticket archive)
   - `FrameParameterSchema` — solo in `docs/tickets/archive/`

2. **Fix P0** — Correggi claim non verificati nei doc e nei ticket:
   - `35-54 ms/frame` è fuorviante — i log mostrano ~1.14s tra frame (include readback+PNG+disk)
   - `85-92% shader cycles saved` non è misurato — mancano `text_active_tiles`, GPU timestamps
   - `In-process remux = 0 ms` non è dimostrato — il benchmark PNG non misura remux
   - `DeviceScheduler: zero contention multi-GPU` non è dimostrato — 1 singolo test non prova multi-GPU
3. **WBH run** — su RTX A4000 + CUDA 13: `tools/verify_chronon_product_linux.sh` 15/15,
   `tools/verify_performance_linux.sh` 5/5 + leak ≤10%.
   Committa `docs/baselines/main-<sha>-baseline.md` + `bench/baselines/main-<sha>-bench.json`.
4. **Docs** — aggiorna `README.md` + `AGENTS.md` hybrid, svuota `CURRENT_STATUS.md` a <100 righe.

Gate: `bash tools/check_architecture.py` 26/26 + `ctest --preset linux-ci` verde stesso SHA.

---

## Fase 1 — FrameDelta + ExecutionResolver (2-3 settimane)

L'idea corretta ma manca il livello architetturale. Il renderer non deve inventarsi
autonomamente cosa è dirty. Una sola authority deve sapere: old bounds, new bounds,
effect expansion, visibility changes, resource changes, background changes.

1. **FrameDeltaCompiler** — Analisi di cosa cambia tra frame:
   - old/new bounds per layer
   - effect expansion (glow, blur che si propagano)
   - visibility changes (layer on/off)
   - resource changes (texture swap, font swap)
   - background changes (clear color, gradient change)
2. **ExecutionResolver** — Produce per ogni frame:
   ```cpp
   enum class FrameExecutionPath {
       CopyGop,        // frame identico → copia GOP packet
       ReuseSurface,   // superficie identica → ripristina da cache
       SparseYuv,      // solo dirty region → composizione parziale YUV
       FullYuv,        // tutto cambiato → composizione YUV completa
       FullRgb         // fallback RGB per effetti CPU-only
   };
   ```
3. **SparseRenderPlan** — Output del FrameDeltaCompiler, consumato dall'executor.
   Contiene: tile bitmask per dirty regions, lista layer da processare, execution path.

**Gate**: `FrameDeltaCompiler` produce output deterministico per input identico.
`ExecutionResolver` sceglie il percorso corretto per ogni scenario di dirty.

---

## Fase 2 — Sparse Tile Engine (2 settimane)

Costruire sul FrameDeltaCompiler con un engine di tile sparse reale.

1. **Dirty Tiles 64×64** — Bitmask invece di union rect in `node_runner.cpp`.
   Il renderer riceve la bitmask dal FrameDeltaCompiler, non la calcola da solo.
2. **Persistent Surfaces** — Surface che sopravvivono tra frame per i tile unchanged.
   `FramebufferPool` shardato con `PhysicalResourcePlan`.
3. **Effect Expansion** — Glow/blur che si espandono beyond i bounds originali:
   il FrameDeltaCompiler deve propagare l'espansione ai tile adiacenti.
4. **Indirect Dispatch** — Vulkan indirect compute dispatch per tile attivi.
   `vkCmdDispatchIndirect` con counter buffer dai tile dirty.

**Gate**: `dirty_tiles_percentage` misurato su benchmark reale.
`tile_reuse_percentage` per frame identici.

---

## Fase 3 — Final Text Pipeline (2-3 settimane)

Consolidare il text pipeline attuale in un percorso unico.

1. **MTSDF Atlas reale** — Confermare che `gpu_glyph_atlas.hpp` con `Mtsdf = 3` (RGBA)
   alloca effettivamente le pagine RGBA8 a runtime. Testare con rendering reale.
2. **Font-size invariant cache** — Cache degli atlas che sopravvive a cambi di font-size
   senza ri-generare tutti i glifi. Key: `font_id + size_bucket + flags`.
3. **Tile Binning unico** — Unificare `text_tile_bin.comp` (binning) e `text_tile_raster.comp`
   (raster) in un pipeline a 2 passi coordinato. Il binning produce la tile-glyph map,
   il raster la consuma.
4. **Drop-in replacement** — `text_batch.comp` loop per-pixel DEVE essere sostituito
   dal tile-based path. Coesistenza attuale: `text_batch.comp` ha ancora
   `for (int i = 0; i < params.glyph_count; ++i)` per pixel.
5. **Elimina duplicate text renderer** — Se `text_batch.comp` e `text_tile_raster.comp`
   producono output identico, eliminare il primo.

**Gate**: benchmark text-compositor-only misura il gain reale del tile-based path.
`text_active_tiles / text_total_tiles` reportato.

---

## Fase 4 — Final Direct YUV (2 settimane)

Composizione diretta in YUV senza passaggi RGBA intermediate.

1. **GpuLayerBatch → CudaYuvLayerBatchExecutor** — Il `GpuLayerBatch` (già implementato)
   viene consumato da un executor che compone direttamente in NV12/P010.
2. **2×2 NV12/P010 kernel** — Compute shader che legge RGB/RGBA layers e scrive
   Y + U/V interleaved. Luminance-based downsampling per UV.
3. **NO RGBA overlay intermediate** — Nessun passaggio intermedio RGBA per la composizione.
   Layer RGB → composizione diretta in NV12.
4. **ColorMetadata awareness** — Il kernel deve rispettare `ColorMetadata` (già in
   `render_surface.hpp:65`) per NV12/P010.

**Gate**: `nv12_to_rgba_frames = 0`, `rgba_to_nv12_frames = 0` nel benchmark video.

---

## Fase 5 — Zero-Copy Async Video Ring (2 settimane)

Ring persistente per decode → compose → encode senza copie CPU.

```
NVDEC N+2  →  COMPOSE N+1  →  NVENC N
zero CPU waits, zero host copies
```

1. **Surface importabile Vulkan→CUDA** — `VK_EXTERNAL_MEMORY_FD + VK_EXTERNAL_SEMAPHORE_FD`
   per importare surface NVDEC in CUDA. `vkGetMemoryFdKHR` → `cudaImportExternalMemory`.
2. **Ring persistente** — 3 slot (decode, compose, encode) con fence/semaphore.
   Nessuna allocazione per-frame.
3. **CUDA/Vulkan synchronization** — Timeline semaphores per coordinare i 3 stage.
4. **Zero CPU waits** — Il CPU non attende mai il completamento di decode/compose/encode.
   usa callback o polling non-bloccante.

**Gate**: `host_upload_bytes = 0`, `host_readback_bytes = 0`,
`encoder_staging_copy_bytes = 0`.

---

## Fase 6 — Native NVENC Packets (2 settimane)

NVENC scrive direttamente i packet nel ring, senza copie intermedie.

1. **Persistent encoder** — L'encoder NVENC rimane aperto per tutta la sessione.
   Nessuna creazione/distruzione per chunk.
2. **Multiple NVENC sessions** — Supporto per più sessioni NVENC contemporanee
   (una per varianti SIMO, vedi Fase 9).
3. **Closed GOP chunks** — Ogni chunk è un GOP chiuso con keyframe iniziale.
   Permette random access nel remux finale.
4. **Optional analytic ME hints** — Motion estimation hints dal FrameDeltaCompiler
   per migliorare la qualità NVENC a parità di bitrate.

**Gate**: `encoder_creation_overhead_ms = 0` per chunk successivi al primo.

---

## Fase 7 — In-Process Packet Assembly (1 settimana)

Assemblaggio finale in-process senza subprocess o file temporanei.

1. **Copied GOP packets** — Packet dal ring NVDEC (video esistente).
2. **NVENC packets** — Packet dal ring NVENC (video renderizzato).
3. **Audio packets** — Packet audio (decodificati o sintetizzati).
4. **libavformat in-process** — Tutti i packet vengono scritti direttamente
   in un `AVFormatContext` senza passare per file temporanei.
5. **NO temp MP4** — Nessun file MP4 temporaneo scritto su disco.
6. **NO ffmpeg subprocess** — Nessun lancio di processi esterni.

**Gate**: `temp_mp4_files_created = 0`, `ffmpeg_subprocess_count = 0`.

---

## Fase 8 — GOP Smart Rendering (1 settimana)

Rendering intelligente basato su GOP-level caching.

1. **Unchanged → packet copy** — Se un GOP non è cambiato, copia i packet
   direttamente dal buffer precedente. Nessun re-encode.
2. **Changed → native render/encode** — Se il GOP è cambiato, render nativo
   + NVENC nativo. Nessun intermedio.
3. **GOP boundary detection** — Il FrameDeltaCompiler identifica i confini GOP
   e segna quali GOP sono dirty.
4. **Partial GOP re-encode** — Se solo alcuni frame del GOP sono dirty,
   re-encode solo quelli e ricompone il GOP.

**Gate**: `unchanged_gop_copy_percentage` misurato. `reencode_percentage` per
benchmark con modifiche locali.

---

## Fase 9 — SIMO Variant Batch (2 settimane)

Master una volta, varianti multiple in parallelo.

1. **Master render una volta** — Il rendering base (testo, camera, effetti)
   viene eseguito una sola volta per il master.
2. **Shared assets/text/decode** — Atlant e texture sono condivisi tra varianti.
   Il decode è condiviso (stesso source video).
3. **Scale/recompose varianti** — Ogni variante applica solo: resize,
   ricomposizione (crop, position), testo diverso, logo diverso.
4. **Multiple NVENC outputs** — Ogni variante ha la sua sessione NVENC
   (da Fase 6). Output separati.

**Gate**: `master_render_count = 1` per N varianti.
`variant_encode_time / N` < `single_encode_time`.

---

## Fase 10 — Multi-GPU (3+ settimane)

Milestone finale. Distribuzione del lavoro su più GPU con load balancing
dinamico e contesti persistenti.

### Prerequisites (tutte le fasi precedenti devono essere completate)

| Prerequisite | Fase | Gate di chiusura |
|---|---|---|
| FrameDeltaCompiler + ExecutionResolver | Fase 1 | Output deterministico per input identico |
| Sparse Tile Engine (64×64 bitmask) | Fase 2 | `dirty_tiles_percentage` misurato |
| Final Text Pipeline (tile binning unico) | Fase 3 | `text_active_tiles / text_total_tiles` |
| Final Direct YUV (NV12/P010 kernel) | Fase 4 | `nv12_to_rgba_frames = 0` |
| Zero-Copy Async Video Ring | Fase 5 | `host_upload_bytes = 0`, `encoder_staging_copy_bytes = 0` |
| Native NVENC Packets (persistent encoder) | Fase 6 | `encoder_creation_overhead_ms = 0` |
| In-Process Packet Assembly | Fase 7 | `temp_mp4_files_created = 0`, `ffmpeg_subprocess_count = 0` |
| GOP Smart Rendering | Fase 8 | `unchanged_gop_copy_percentage` misurato |
| SIMO Variant Batch (multi-session NVENC) | Fase 9 | `master_render_count = 1` per N varianti |

**Regola**: Non avviare Fase 10 finché tutte le fasi 1-9 non hanno
`PASS` sullo stesso SHA. Ogni fase successiva si basa sulla precedente.

### Componenti

1. **DeviceSessionPool** — Pool di sessioni CUDA/Vulkan per ogni GPU.
   Sessioni persistenti, nessuna creazione per-frame. Ogni sessione
   possiede: CUDA context, Vulkan device, command queue, NVENC encoder.
   ```cpp
   class DeviceSessionPool {
       std::vector<DeviceSession> sessions_;  // una per GPU
       std::mutex mu_;
   public:
       DeviceSession* acquire(DeviceResourceVector requirements);
       void release(DeviceId id, DeviceSession* session);
   };
   ```

2. **ResourceVector scheduler** — Estensione del `DeviceScheduler` (già
   implementato con `register_device`, `reserve`, `release`, `calculate_pressure`).
   Aggiunge: `ResourceVector` per: compute_units, vram_bytes,
   nvdec_sessions, nvenc_sessions. Pressure-based placement.
   ```cpp
   struct ResourceVector {
       float compute_units;
       uint64_t vram_bytes;
       uint32_t nvdec_sessions;
       uint32_t nvenc_sessions;
   };
   ```

3. **Chunk placement** — Ogni chunk di video viene assegnato alla GPU
   con più risorse disponibili. L'ExecutionResolver (Fase 1) produce
   `FrameExecutionPath::SparseYuv` o `FrameExecutionPath::FullYuv`,
   e il chunk placement sceglie la GPU. Load balancing dinamico:
   ```cpp
   struct ChunkAssignment {
       DeviceId gpu_id;
       FrameExecutionPath path;
       uint64_t estimated_vram;
       uint32_t estimated_nvenc_slots;
   };
   ```

4. **Persistent contexts** — CUDA/Vulkan context per GPU persistono
   per tutta la sessione. Nessuna creazione/distruzione per-chunk.
   Timeline semaphores per sincronizzazione inter-GPU.

5. **Cross-GPU synchronization** — I 3 stage (NVDEC → COMPOSE → NVENC)
   possono essere distribuiti su GPU diverse. Il ring persistente
   (Fase 5) viene esteso con `CrossGpuRingSlot`:
   ```cpp
   struct CrossGpuRingSlot {
       DeviceId decode_gpu;
       DeviceId compose_gpu;
       DeviceId encode_gpu;
       VkSemaphore timeline;
   };
   ```

### Metriche chiave

| Metrica | Target | Gate |
|---|---|---|
| `multi_gpu_speedup / gpu_count` | > 0.7 (efficienza > 70%) | `multi_gpu_efficiency` |
| `zero_contention` | Nessuna attesa tra worker | `cross_gpu_contention_events = 0` |
| `device_utilization` | > 80% per GPU | `avg_gpu_utilization` |
| `chunk_load_balance` | < 20% varianza | `chunk_load_stddev / mean` |
| `memory_pressure_events` | = 0 | `oom_events = 0` |

### Test e validazione

```bash
# Test unitari DeviceScheduler (già esistenti)
cctest -R device_scheduler --output-on-failure

# Test multi-GPU (da implementare)
cctest -R multi_gpu --output-on-failure

# Benchmark multi-GPU
bash bench/benchmark_pipeline_stages.sh --stage full-video-export --frames 60 --gpu-count 2
bash bench/benchmark_pipeline_stages.sh --stage full-video-export --frames 60 --gpu-count 4

# Certificazione zero-copy su multi-GPU
bash tools/verify_zero_copy_end_to_end.sh --gpu-count 2
```

### Architettura target

```
┌─────────────────────────────────────────────────────────┐
│                    DeviceSessionPool                     │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐ │
│  │ GPU 0    │  │ GPU 1    │  │ GPU 2    │  │ GPU N    │ │
│  │ CUDA ctx │  │ CUDA ctx │  │ CUDA ctx │  │ CUDA ctx │ │
│  │ VkDevice │  │ VkDevice │  │ VkDevice │  │ VkDevice │ │
│  │ NVENC    │  │ NVENC    │  │ NVENC    │  │ NVENC    │ │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘ │
│                                                         │
│  ┌─────────────────────────────────────────────────────┐│
│  │           ResourceVector Scheduler                  ││
│  │  pressure = f(compute, vram, nvdec, nvenc)          ││
│  │  placement = min(pressure) across GPUs              ││
│  └─────────────────────────────────────────────────────┘│
│                                                         │
│  ┌─────────────────────────────────────────────────────┐│
│  │           Chunk Placement Engine                    ││
│  │  chunk_0 → GPU 0 (SparseYuv)                       ││
│  │  chunk_1 → GPU 1 (FullYuv)                         ││
│  │  chunk_2 → GPU 0 (CopyGop)                         ││
│  │  chunk_3 → GPU 2 (SparseYuv)                       ││
│  └─────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────┘
```

### Scaling atteso

| GPU count | Speedup atteso | Efficiency |
|---|---|---|
| 1 | 1.0x (baseline) | 100% |
| 2 | 1.6-1.8x | 80-90% |
| 4 | 2.8-3.2x | 70-80% |
| 8 | 5.0-5.6x | 63-70% |

**Note**: L'efficiency diminuisce con più GPU per overhead di:
- Sincronizzazione inter-GPU (timeline semaphores)
- Bilanciamento del carico (chunk placement overhead)
- Memoria condivisa (texture atlas replicati)

**Gate finale**: `CHRONON_MULTI_GPU_PASS` su `main` con 2+ GPU,
tutte le fasi 1-9 precedentemente PASS.

---

## Controlled Demolition — DELETE TARGETS

Questa è la lista ufficiale dei candidati alla rimozione. Una voce può passare
da `TODO` a `DONE` solo dopo questa sequenza atomica:

```text
replacement certificato
    → telemetria dimostra l'uso del replacement
    → golden/correctness equivalenti verdi
    → vecchio path deprecato e fallback reso esplicito
    → DELETE del vecchio path
```

Un fallback non va cancellato solo perché è lento: va isolato, nominato e
selezionato da `FallbackPolicy { Forbid, Warn, Allow }`. I benchmark usano
`Forbid`; production sceglie `Warn` o `Allow` secondo configurazione. Nessuna
voce di questa lista è certificata implicitamente dalla sua presenza nel piano.

### Hot path

- [ ] **Scene/frame** — eliminare `Scene` per-frame quando la topology è già
      compilata; il fallback per topology arbitraria resta separato.
- [ ] **RenderGraph/frame** — eliminare build, walk e pass discovery per-frame;
      il loop consuma `FrameParameters`, `FrameDelta`, `ExecutionDecision` e
      `CommandPlan`.
- [ ] **Heap allocation/frame** — portare le allocazioni steady-state a zero:
      tabelle parametri, tile mask/liste, glyph refs, motion hints, packet
      buffers, encoder queue, surface ring e telemetry sono preallocati.
- [ ] **String/hash lookup** — risolvere gli identificatori in integer handle
      durante compile/preparation; nessuna stringa, confronto o path resolution
      nel loop caldo.
- [ ] **Text shaping/frame** — spostare HarfBuzz, segmentazione Unicode e line
      breaking al cambio di layout; transform-only aggiorna solo parametri.
- [ ] **Glyph generation/upload steady-state** — dopo warmup,
      `glyph_atlas_upload_bytes/frame = 0`, anche tra job compatibili del daemon.
- [ ] **Global GPU synchronization/frame** — rimuovere `vkDeviceWaitIdle`,
      `cudaDeviceSynchronize` e `cuStreamSynchronize` dal steady-state; tenerli
      solo per shutdown, debug, recovery e test.

### Video

- [ ] **CPU `Framebuffer` nel native path** — percorso certificato:
      `RenderSurfaceHandle → NVENC`; `Framebuffer` resta per CPU renderer,
      immagini, debug, golden e fallback software.
- [ ] **RGBA intermediate fast path** — `GpuLayerBatch → direct YUV
      compositor → NV12/P010`; eliminare clear, superfici e passaggi RGB
      intermedi quando l'effetto non li richiede.
- [ ] **YUV↔RGB fast-path conversions** — eliminare conversioni non necessarie;
      `FullRgb` resta solo per effetti che lo richiedono davvero.
- [ ] **Temporary `chunk_XXXX.mp4`** — `EncodedPacket → central
      PacketAssembler`; eliminare file temporanei, reopen, stream parsing,
      cleanup directory e filename generation.
- [ ] **PNG intermediates** — eliminare i file PNG temporanei usati come
      trasporto della pipeline video; conservarli solo per export immagine,
      debug e golden tests.
- [ ] **Per-chunk renderer/encoder creation** — acquisire una `DeviceSession`
      dal pool persistente; niente setup Vulkan/CUDA, warmup atlas o encoder
      creation nel worker chunk.
- [ ] **Native-path FFmpeg subprocess** — isolare la pipe in
      `video/fallback/`; il native path usa solo packet assembly in-process.

### Text

- [ ] **Duplicate text renderer** — adattare l'input legacy a
      `CanonicalTextBatch`, certificare `GpuGlyphStatic + TextRunDynamic +
      MTSDF + tile binning`, poi eliminare il renderer duplicato.
- [ ] **Full-canvas × glyph loop** — eliminare il loop per-pixel di
      `text_batch.comp` quando il tile path produce golden equivalenti.
- [ ] **Font-size nella MTSDF identity** — la chiave diventa
      `face + glyph-id + variation + representation + generation-profile`;
      la scala resta runtime.
- [ ] **Runtime bitmap scaling e styled bitmap cache ridondanti** — rimuovere
      i path legacy solo dopo parity del distance-field path.

### Smart render e media pass-through

- [ ] **Full-frame default** — `FrameDeltaCompiler`/resolver deve scegliere
      `COPY`, `REUSE`, `SPARSE`, `FULL YUV` o `FULL RGB`; un dirty lower-third
      non implica redraw del canvas.
- [ ] **Unchanged GOP decode/render/encode** — per GOP intatto usare packet
      copy con timestamp mapping e mux; NVDEC, compositor e NVENC restano
      fuori da quel tratto.
- [ ] **Unchanged audio processing** — per audio pass-through usare packet
      remap; decode PCM, filtri e AAC encode restano solo sui tratti necessari.
- [ ] **SIMO preparation duplication** — compile, decode, shape, asset upload e
      atlas warmup una volta; le varianti fanno solo fan-out/recompose/output.

### Infrastructure e benchmark

- [ ] **Silent fallback** — eliminare i fallback impliciti, non tutti i
      fallback: ogni downgrade deve produrre policy, diagnostic e telemetry.
- [ ] **Per-job compilation** — usare `CompiledProgramCache` per fingerprint
      di composition, asset, compile options e backend capabilities; nessuna
      ricompilazione nel job path quando il programma è invariato.
- [ ] **Duplicate benchmark plumbing** — mantenere `chronon3d_bench` e il
      benchmark E2E, ma condividere `BenchmarkCore`, metric model, serializer,
      scenario metadata, percentile calculator e hardware description.
- [ ] **Dead shaders, stale counters e duplicate registries/resolvers** —
      rimuovere solo dopo source audit, test/gate verdi e verifica che la sede
      canonica sia unica.

### Esplicitamente conservati

`CPU renderer` (oracle/fallback/correctness), `FFmpeg fallback` (formati o
codec non coperti dal native path), `C ABI v2` e `chronon3d_bench` non sono
delete target. La loro superficie va eventualmente ridotta o resa esplicita,
ma non rimossa come parte di questa demolizione.

---

## Cosa NON fare

- Nuovi effetti/preset/binding/plugin prima di Fase 0 verde (ordine `ROADMAP.md:30`).
- Nuovi singleton/registry/cache senza ADR (`AGENTS.md:84`, `tools/architecture_rules.toml`).
- `#include <msdfgen>/<libtess2>/<unicode>` senza ADR.
- Percentuali manuali (`AGENTS.md:85`) — solo `PASS/FAIL/PARTIAL/NOT RUN`.
- Dichiarare feature completate senza test che le verifichino.
- Aggiungere claim non misurati nei documenti.

---

## Ordine con gain atteso

```
Fase 0 (certificazione) → Fase 1 (FrameDelta) → Fase 2 (Sparse Tile)
→ Fase 3 (Text Pipeline) → Fase 4 (Direct YUV) → Fase 5 (Zero-Copy Ring)
→ Fase 6 (Native NVENC) → Fase 7 (Packet Assembly) → Fase 8 (GOP Smart)
→ Fase 9 (SIMO) → Fase 10 (Multi-GPU)
```

**Target**: `4.11s → ~2.7s` (Fase 0-5) poi sotto `2.53s` NVDEC→NVENC (Fase 6-8)
poi `N×` con SIMO e Multi-GPU (Fase 9-10).

---

## Verifica per ogni commit

```bash
bash tools/check_architecture.py
ctest -R "chronon3d_tests_fast|backend_registry|compositor|render_graph" --output-on-failure
bash tools/verify_performance_linux.sh
bash tools/run_cuda_vulkan_external_memory_probe.sh # su RTX A4000
bash tools/verify_zero_copy_end_to_end.sh # zero-copy certification
```

**Benchmark multipli** (no PNG intermediates):
```bash
bash bench/benchmark_pipeline_stages.sh --stage render-null --frames 60
bash bench/benchmark_pipeline_stages.sh --stage text-compositor --frames 60
bash bench/benchmark_pipeline_stages.sh --stage nv12-compositor --frames 60
bash bench/benchmark_pipeline_stages.sh --stage nvenc-native --frames 60
bash bench/benchmark_pipeline_stages.sh --stage render-to-nvenc --frames 60
bash bench/benchmark_pipeline_stages.sh --stage full-video-export --frames 60
```

**Zero-copy certification** (8 gate obbligatori):
```bash
bash tools/verify_zero_copy_end_to_end.sh
# Gate: host_upload_bytes=0, host_readback_bytes=0, nv12_to_rgba_frames=0,
#       rgba_to_nv12_frames=0, encoder_staging_copy_bytes=0,
#       gpu_surface_copy_frames=0, cpu_pixel_readback_bytes=0,
#       video_surface_upload_bytes=0
```

Archivio: `docs/tickets/archive/` (113 ticket), `docs/baselines/archive/` (17 baselines).

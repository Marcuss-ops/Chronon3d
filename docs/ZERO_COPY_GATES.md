# Zero-Copy End-to-End Certification

## Obiettivo

Certificare che il video pipeline Chronon3D achieve **zero-copy end-to-end**:

```
NVDEC → native surface → CUDA/Vulkan compose → NVENC
```

Senza copie CPU/GPU evitabili e senza intermedi di staging. La conversione
colore necessaria del FullGraph (`RGBA→NV12`) resta sul device GPU; il path
DirectYUV può invece evitarla del tutto.

## I 15 Gate Mandatori (Phase 13)

**Non esistono gate opzionali** (Phase 13): ogni gate sottostante è mandatory.
Un gate il cui valore non può essere verificato produce `ZERO_COPY_BLOCKED`,
mai un PASS. I contatori di copia e conversione CPU devono essere **= 0**;
`rgba_to_nv12_frames` è invece **>= 0** perché nel FullGraph la conversione
colore GPU è necessaria; i due contatori di identità nativa devono essere
**> 0**, e le tre identità di esecuzione devono corrispondere esattamente:

| # | Gate | Metrica | Target | Cosa misura |
|---|---|---|---|---|
| 1 | **host_upload_bytes** | `gpu_upload_bytes` | = 0 | Byte caricati da CPU→GPU |
| 2 | **host_readback_bytes** | `gpu_readback_bytes` | = 0 | Byte letti da GPU→CPU |
| 3 | **nv12_to_rgba_frames** | `nv12_to_rgba_frames` | = 0 | Frame convertiti NV12→RGBA |
| 4 | **rgba_to_nv12_frames** | `rgba_to_nv12_frames` | ≥ 0, GPU | Conversione colore GPU necessaria nel FullGraph |
| 5 | **software_color_convert_frames** | `software_color_convert_frames` | = 0 | Nessuna conversione software |
| 6 | **cpu_pixel_conversion_ms** | `cpu_pixel_conversion_ms` | = 0 | Nessuna conversione pixel CPU |
| 7 | **encoder_staging_copy_bytes** | `encoder_staging_copy_bytes` | = 0 | Byte copiati per staging encoder |
| 8 | **gpu_surface_copy_frames** | `gpu_surface_copy_frames` | = 0 | Frame con copie superficie GPU |
| 9 | **cpu_pixel_readback_bytes** | `cpu_pixel_readback_bytes` | = 0 | Byte pixel letti da CPU |
| 10 | **video_surface_upload_bytes** | `video_surface_upload_bytes` | = 0 | Byte superficie video caricati |
| 11 | **native_surface_frames** | `gpu_native_surface_frames` | > 0 | Frame con superfici GPU native |
| 12 | **native_encode_frames** | `gpu_native_encode_frames` | > 0 | Frame codificati nativamente via NVENC |
| 13 | **effective_backend** | `effective_backend` | = `vulkan` | Il backend effettivo di rendering |
| 14 | **encoder_backend** | `encoder_backend` | = `nvenc` | L'autorità encoder nativa |
| 15 | **surface_handoff_path** | `surface_handoff_path` | = `direct` | Handoff superficie senza indirezione |

### Autorità: il timing sidecar, non il log

Il `*.timing.json` sidecar è l'autorità dei contatori. In modalità
certificazione (`tools/verify_zero_copy_end_to_end.sh`) un sidecar mancante
produce **`ZERO_COPY_BLOCKED`** — mai un fallback verso il parsing del
`render.log`. Il log resta diagnostico; la certificazione richiede il
sidecar.

## Gate 1 — due lane, stessa semantica (Z-wave F5/F7)

Il gate 1 (`host_upload_bytes`) misura gli upload CPU→GPU **senza
riclassificare i contatori**. Ha due metà distinte, esportate nel timing
sidecar sotto `job.gpu`:

- `gpu_upload_bytes` — metà Vulkan: backend upload stats
  (`vulkan_backend_stats.cpp`);
- `cuda_host_upload_bytes` — metà CUDA: il CUDA image cache di
  `VideoDeviceRuntime::get_or_upload_image` (`cuMemcpyHtoD`, counted at the
  single upload authority).

La certificazione usa **due finestre**: `setup/prewarm` (asset, atlas e
upload di overlay caricati una volta) e `steady-state` (i frame certificati).
Il report del job non nasconde gli upload di setup: la finestra certificata
è quella di steady-state, e il tool riporta `cuda_host_upload_bytes` come
informazione a fianco del gate 1. `tools/verify_zero_copy_end_to_end.sh`
segue già questa semantica (warmup renderer + framebuffer prima del loop).

## Uso

```bash
# Certificazione automatica (esegue render e verifica)
bash tools/verify_zero_copy_end_to_end.sh

# Con parametri specifici
bash tools/verify_zero_copy_end_to_end.sh --scene DarkGridBackground --frames 60

> **Scene note (2026-09-07):** il default storico `TextPlaceAnimatedCenter`
> non è più registrato in questo repository (la tree content/ è stata
> esternalizzata); il default ora è la builtin `DarkGridBackground`,
> registrata sempre e senza asset esterni. `CHRONON3D_BENCH_SCENE` resta
> l'override per host con content pack.

# Da report JSON esistente
bash tools/verify_zero_copy_end_to_end.sh --report-json /path/to/report.json
```

## Exit Codes

| Code | Significato |
|---|---|
| `0` | `ZERO_COPY_PASS` — tutti i gate obbligatori = 0 |
| `1` | `ZERO_COPY_FAIL` — almeno un gate obbligatorio ≠ 0 |
| `2` | `ZERO_COPY_BLOCKED` — report mancante o gate non verificabili |

## Pipeline Reale vs Pipeline PNG

### Pipeline PNG (legacy)
```
GPU render → CPU readback → PNG encode → disk I/O
           ↑                 ↑              ↑
    host_readback_bytes   PNG overhead   I/O overhead
    ≠ 0 (SBAGLIATO)      (ELIMINATO)    (ELIMINATO)
```

### Pipeline Zero-Copy (target)
```
NVDEC → native surface → CUDA/Vulkan compose → NVENC → MP4
       ↑                  ↑                     ↑
  gpu_native_surface   GPU color conversion  gpu_native_encode
  frames > 0           (FullGraph allowed)  frames > 0
                       CPU conversion = 0
                       surface copies = 0
                       encoder staging = 0
```

## Roadmap Alignment

Questi gate supportano le fasi del `docs/CHRONON_PLAN.md`:

| Fase | Gate associato | Stato |
|---|---|---|
| Fase 0 | Tutti (certificazione baseline) | Eseguita 2026-09-16 su host GPU: `ZERO_COPY_PASS` (lane FullGraph) |
| Fase 4 | nv12_to_rgba_frames, rgba_to_nv12_frames | Strumentati e misurati: `0` / `30` (GPU, FullGraph) |
| Fase 5 | host_upload_bytes, host_readback_bytes, gpu_surface_copy_frames | Misurati: `0` / `0` / `0` |
| Fase 6 | encoder_staging_copy_bytes, gpu_native_encode_frames | Misurati: `0` / `30` (> 0) |

La lane DirectYUV a conversione zero resta da certificare end-to-end e richiede
un piano con sorgente video (vedi §Metriche Attuali).

## Metriche Attuali (misurate 2026-09-16, RTX A4000 16 GB, driver 595.91.07)

Certificazione del 2026-09-16 (`bash tools/verify_zero_copy_end_to_end.sh --frames 30`,
scena builtin `DarkGridBackground`, `--backend vulkan --hardware nvenc
--encoder-backend native --gpu-hot-path-mode require_gpu_native`, warmup 10):

```
GPU-native 1920x1080 30f (lane FullGraph, device nativo):
  host_upload_bytes             = 0   ✅
  host_readback_bytes           = 0   ✅
  nv12_to_rgba_frames           = 0   ✅
  rgba_to_nv12_frames           = 30  ✅ (conversione colore GPU, ammessa nel FullGraph)
  software_color_convert_frames = 0   ✅
  cpu_pixel_conversion_ms       = 0   ✅
  encoder_staging_copy_bytes    = 0   ✅ (nessuna copia di staging encoder)
  gpu_surface_copy_frames       = 0   ✅ (nessuna blit D2D Vulkan→CUDA)
  cpu_pixel_readback_bytes      = 0   ✅
  video_surface_upload_bytes    = 0   ✅
  effective_backend             = vulkan   ✅
  encoder_backend               = nvenc    ✅
  surface_handoff_path          = direct   ✅
  gpu_native_surface_frames     = 30  (> 0) ✅
  gpu_native_encode_frames      = 30  (> 0) ✅
  cuda_host_upload_bytes        = 0        (finestra steady-state)
  verdict: ZERO_COPY_PASS (16 PASS / 0 FAIL / 0 BLOCKED)
```

**Stato**: la lane di produzione FullGraph/NVENC nativa è **certificata
zero-copy** sul host GPU: nessuna copia di staging, nessuna blit D2D, nessun
readback e nessuna conversione software. Il residuo storico
(`encoder_staging_copy_bytes`/`gpu_surface_copy_frames ≠ 0` del percorso D2D
Vulkan→CUDA→NVENC) **non è più osservato**: non aggiornare questo documento con
quei numeri senza ri-misurarli.

### Cosa resta sul zero-copy

- `rgba_to_nv12_frames = 30` è la conversione colore GPU necessaria del
  FullGraph; la lane a **conversione zero** è DirectYUV, e il suo certificato
  (`--direct-yuv`) richiede un piano con **sorgente video** (NVDEC). La scena
  builtin è procedurale (`video_source_requested_frames = 0`), quindi il
  resolver rifiuta la richiesta in modo dichiarato e contato:
  `job.execution_path = full_graph_native`,
  `job.execution_reason = direct_yuv_capability_rejected`,
  `job.execution_downgrade_count = 1`, `job.gpu.decoder_backend = software` —
  e il gate `--direct-yuv` riporta correttamente `ZERO_COPY_FAIL` (3 gate).
  Certificare DirectYUV end-to-end richiede un `--plan` con sorgente video
  (fixture NVDEC), non un cambio di codice.
- Il residuo `copy_gop_source`/oracle e la decisione sulla lane di superficie
  restano tracciati in `docs/FOLLOWUP_TICKETS.md`.

## Strumentazione dei gate (2026-08-28)

I gate 3 (`nv12_to_rgba_frames`) e 4 (`rgba_to_nv12_frames`) non erano
strumentati: i contatori non esistevano e il gate 6 (`gpu_surface_copy_frames`)
non veniva mai incrementato, quindi la certificazione passava senza misurare
le conversioni reali. Ora:

- `nv12_to_rgba_frames` — incrementato in `media::video::CudaNv12Compositor::composite()`
  (kernel `nv12_to_rgba` / `p010_to_rgba`; Wave 1D ha unificato l'autorità
  NV12 in `src/media/video/compositor/`, ex `CudaNv12SurfaceCompositor` Vulkan).
- `rgba_to_nv12_frames` — incrementato in
  `media::video::CudaNv12Compositor::composite_surface_to_nv12()`
  (kernel `rgba_surface_to_nv12_2x2` / `rgba_u8_surface_to_nv12_2x2`).
- `gpu_surface_copy_frames` — incrementato in
  `VulkanBackend::Impl::copy_surface_to_cuda_encoder()` (la blit D2D
  `vkCmdBlitImage` Vulkan→CUDA).

I tre contatori sono esposti nel report `*.timing.json` sotto `job.gpu`
e vengono letti da `tools/verify_zero_copy_end_to_end.sh`.

**Implicazione**: il percorso di encode nativo attuale (`write_native_surface_impl`
→ `composite_surface_to_nv12`) fa ancora una conversione RGBA→NV12 per frame,
quindi `rgba_to_nv12_frames` e `gpu_surface_copy_frames` risultano `≠ 0` finché
non viene usato il percorso direct-YUV (`composite_direct_nv12*`), che compone
l'overlay direttamente nel dominio NV12 senza passare da RGBA.

## Percorso zero-copy target (DirectYUV)

Il percorso che azzera la conversione colore oltre ai gate di copia è già
implementato nei kernel CUDA:

```
NVDEC (NV12/P010) → media::video::CudaNv12Compositor::composite_direct_nv12*
    → Vulkan composita l'overlay direttamente nell'immagine YUV
    → NVENC (nessuna copia RGBA intermedia, nessuna blit D2D)
```

Per attivarlo nel pipeline di export serve sostituire, in
`NativeAvEncoder::write_native_surface_impl`, la coppia
`copy_surface_to_cuda_encoder` + `composite_surface_to_nv12` con
`composite_direct_nv12*` (o far scrivere il grafo Vulkan direttamente
sulla superficie CUDA-exportable).

## Integrazione CI

```yaml
# .github/workflows/ci.yml
- name: Zero-copy certification
  run: bash tools/verify_zero_copy_end_to_end.sh --frames 30
```

## Equivalence gate per Z3 (rimozione D2D per-frame)

`tools/verify_z3_equivalence.sh` certifica che la rimozione della D2D copy
per-frame (Z3: il grafo compone direttamente nella encode surface dello slot)
non cambia i pixel resi.  Due registrazioni sulla stessa corpus di scene
(render PNG frame-by-frame sul lane Vulkan + export `require_gpu_native` con
`--video`), poi `--compare`: la parità dei frame resi DEVE essere byte-
identica e sul candidate `gpu_surface_copy_frames == 0` con
`rgba_to_nv12_frames` invariato.  Exit codes: `EQUIVALENCE_PASS` /
`EQUIVALENCE_FAIL` / `EQUIVALENCE_BLOCKED`.  Il confronto usa hash dei pixel
resi, mai l'MP4 codificato (NVENC non è bit-exact tra run).

```bash
# Reference (main pre-Z3) e candidate (branch Z3) sull'host GPU:
bash tools/verify_z3_equivalence.sh --record /tmp/z3-ref --video
bash tools/verify_z3_equivalence.sh --record /tmp/z3-cand --video
bash tools/verify_z3_equivalence.sh --compare /tmp/z3-ref /tmp/z3-cand
```

## Confronto con Benchmark Separati

I gate zero-copy sono complementari ai benchmark pipeline stages:

| Benchmark | Cosa misura | Gate zero-copy |
|---|---|---|
| `render-null` | Render pass puro | — |
| `text-compositor` | Text composition | — |
| `nv12-compositor` | NV12 composition | nv12_to_rgba = 0 |
| `nvenc-native` | NVENC encoding | encoder_staging = 0 |
| `render-to-nvenc` | Render → NVENC | host_readback = 0 |
| `full-video-export` | Pipeline completa | Tutti i gate |

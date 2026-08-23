# Zero-Copy End-to-End Certification

## Obiettivo

Certificare che il video pipeline Chronon3D achieve **zero-copy end-to-end**:

```
NVDEC → native surface → CUDA/Vulkan compose → NVENC
```

Senza copie CPU, senza conversioni di formato, senza intermedi.

## I 8 Gate Obbligatori

Tutti i gate devono essere **= 0** per `ZERO_COPY_PASS`:

| # | Gate | Metrica | Target | Cosa misura |
|---|---|---|---|---|
| 1 | **host_upload_bytes** | `gpu_upload_bytes` | = 0 | Byte caricati da CPU→GPU |
| 2 | **host_readback_bytes** | `gpu_readback_bytes` | = 0 | Byte letti da GPU→CPU |
| 3 | **nv12_to_rgba_frames** | `nv12_to_rgba_frames` | = 0 | Frame convertiti NV12→RGBA |
| 4 | **rgba_to_nv12_frames** | `rgba_to_nv12_frames` | = 0 | Frame convertiti RGBA→NV12 |
| 5 | **encoder_staging_copy_bytes** | `encoder_staging_copy_bytes` | = 0 | Byte copiati per staging encoder |
| 6 | **gpu_surface_copy_frames** | `gpu_surface_copy_frames` | = 0 | Frame con copie superficie GPU |
| 7 | **cpu_pixel_readback_bytes** | `cpu_pixel_readback_bytes` | = 0 | Byte pixel letti da CPU |
| 8 | **video_surface_upload_bytes** | `video_surface_upload_bytes` | = 0 | Byte superficie video caricati |

## I 2 Gate Opzionali

Informativi, non bloccano la certificazione:

| # | Gate | Metrica | Target | Cosa misura |
|---|---|---|---|---|
| 9 | **native_surface_frames** | `gpu_native_surface_frames` | > 0 | Frame con superfici GPU native |
| 10 | **native_encode_frames** | `gpu_native_encode_frames` | > 0 | Frame codificati nativamente via NVENC |

## Uso

```bash
# Certificazione automatica (esegue render e verifica)
bash tools/verify_zero_copy_end_to_end.sh

# Con parametri specifici
bash tools/verify_zero_copy_end_to_end.sh --scene TextPlaceAnimatedCenter --frames 60

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
  gpu_native_surface   zero conversion      gpu_native_encode
  frames > 0           nv12_to_rgba = 0     frames > 0
                       rgba_to_nv12 = 0
                       encoder_staging = 0
```

## Roadmap Alignment

Questi gate supportano le fasi del `docs/CHRONON_PLAN.md`:

| Fase | Gate associato | Stato |
|---|---|---|
| Fase 0 | Tutti (certificazione baseline) | Da eseguire |
| Fase 4 | nv12_to_rgba_frames, rgba_to_nv12_frames | Da implementare |
| Fase 5 | host_upload_bytes, host_readback_bytes, gpu_surface_copy_frames | Da implementare |
| Fase 6 | encoder_staging_copy_bytes, gpu_native_encode_frames | Da implementare |

## Metriche Attuali (da CURRENT_STATUS.md)

```
GPU-native 1920x1080 960f watermark+subtitle:
  gpu_readback_bytes = 0        ✅
  fallback = 0                  ✅
  effective_backend = vulkan    ✅
  encoder_staging_copy_bytes ≠ 0  ❌ (residuo D2D Vulkan→CUDA→NVENC)
  gpu_surface_copy_frames ≠ 0    ❌ (residuo D2D Vulkan→CUDA→NVENC)
```

**Stato**: Zero-copy parzialmente raggiunto. I gate 1-2 passano, i gate 5-6 falliscono.
Il residuo è il percorso D2D Vulkan→CUDA→NVENC che deve essere eliminato
dalla Fase 5 (Zero-Copy Async Video Ring).

## Integrazione CI

```yaml
# .github/workflows/ci.yml
- name: Zero-copy certification
  run: bash tools/verify_zero_copy_end_to_end.sh --frames 30
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

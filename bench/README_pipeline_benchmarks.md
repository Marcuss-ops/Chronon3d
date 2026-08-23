# Pipeline Stage Benchmarks

Benchmark multipli separati per misurare Chronon3D end-to-end **senza PNG intermediates**.

## Perché benchmark separati

Il benchmark PNG classico misura:

```
GPU render → CPU readback → PNG encode → disk I/O
```

Ma nel prodotto finale vogliamo:

```
GPU render → NV12/P010 → NVENC → MP4
```

I 6 benchmark qui sotto isolano ogni stage della pipeline reale.

## I 6 Stages

| # | Stage | Cosa misura | Cosa NON misura |
|---|---|---|---|
| 1 | `render-null` | Render pass puro (CPU/GPU) | Output, encode, I/O |
| 2 | `text-compositor` | Atlas + shaping + raster testo | Altri layer, encode |
| 3 | `nv12-compositor` | Composizione RGB→YUV diretta | Render, encode |
| 4 | `nvenc-native` | Encoding NVENC puro | Render, composizione |
| 5 | `render-to-nvenc` | Render nativo → NVENC (senza copie) | Intermediate copies |
| 6 | `full-video-export` | Pipeline completa | — |

## Uso Rapido

```bash
# Singolo stage
bash bench/benchmark_pipeline_stages.sh --stage render-null --frames 60

# Tutti i 6 stages
bash bench/benchmark_pipeline_stages.sh --stage all --frames 60

# Con ripetizioni per statistics
bash bench/benchmark_pipeline_stages.sh --stage nvenc-native --frames 120 --repetitions 5

# Scena specifica
bash bench/benchmark_pipeline_stages.sh --stage full-video-export --scene TextPlaceAnimatedCenter --frames 60
```

## Variabili d'Ambiente

| Variabile | Default | Descrizione |
|---|---|---|
| `CHRONON3D_CLI` | auto-detect | Path al binario `chronon3d_cli` |
| `CHRONON3D_BENCH_STAGE` | — | Stage da eseguire |
| `CHRONON3D_BENCH_FRAMES` | 60 | Numero di frame da misurare |
| `CHRONON3D_BENCH_WARMUP` | 10 | Frame di warmup (scartati) |
| `CHRONON3D_BENCH_REPETITIONS` | 3 | Numero di ripetizioni |
| `CHRONON3D_BENCH_SCENE` | TextPlaceAnimatedCenter | Composizione da benchmarkare |
| `CHRONON3D_BENCH_WIDTH` | 1920 | Larghezza frame |
| `CHRONON3D_BENCH_HEIGHT` | 1080 | Altezza frame |
| `CHRONON3D_BENCH_OUTPUT` | /tmp/bench_stages | Directory output |
| `CHRONON3D_BENCH_ASSETS` | — | Assets root path |
| `CHRONON3D_BENCH_FPS_NUM` | 30 | Numeratore FPS |
| `CHRONON3D_BENCH_FPS_DEN` | 1 | Denominatore FPS |

## Output

Ogni stage produce un JSON con:

```json
{
  "stage": "render-null",
  "scene": "TextPlaceAnimatedCenter",
  "commit": "9142c627",
  "timestamp": "20260823_153000",
  "frames": 60,
  "warmup": 10,
  "repetitions": 3,
  "resolution": "1920x1080",
  "fps": "30/1",
  "total_wall_ms": 1234,
  "frame_p50_ms": 18.5,
  "frame_p95_ms": 22.1,
  "peak_rss_mb": 512.0
}
```

## Metriche Chiave da Confrontare

| Metrica | Stadio | Target |
|---|---|---|
| `render-null` wall time | Fase 0-2 | < 20ms/frame |
| `text-compositor` wall time | Fase 3 | < 10ms/frame |
| `nv12-compositor` wall time | Fase 4 | < 5ms/frame |
| `nvenc-native` wall time | Fase 6 | < 5ms/frame |
| `render-to-nvenc` wall time | Fase 5 | < 15ms/frame |
| `full-video-export` wall time | — | < 25ms/frame |

## Gate di Certificazione

Per ogni commit, eseguire:

```bash
# Benchmark minimo (2 stages)
bash bench/benchmark_pipeline_stages.sh --stage render-null --frames 30
bash bench/benchmark_pipeline_stages.sh --stage full-video-export --frames 30

# Benchmark completo (prima di release)
bash bench/benchmark_pipeline_stages.sh --stage all --frames 60 --repetitions 5
```

## Confronto con Benchmark PNG

Il benchmark PNG classico (`chronon3d_cli render ... -o output.png`) misura:

```
render + readback + PNG encode + disk write ≈ 1.14s/frame (osservato)
```

I benchmark separati misurano:

```
render-null:       ~20ms   (solo render)
text-compositor:   ~10ms   (solo testo)
full-video-export: ~25ms   (render+compose+encode)
```

La differenza tra `render-null` e `full-video-export` è il costo reale della pipeline video.

## Roadmap Alignment

Questi benchmark supportano le fasi del `docs/CHRONON_PLAN.md`:

- **Fase 0**: `render-null` per certificare la baseline
- **Fase 3**: `text-compositor` per misurare il gain del tile-based path
- **Fase 4**: `nv12-compositor` per misurare il gain del Direct YUV
- **Fase 5**: `render-to-nvenc` per misurare il gain dello zero-copy ring
- **Fase 6**: `nvenc-native` per misurare il gain del persistent encoder

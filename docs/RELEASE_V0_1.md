# Chronon3D — Release v0.1 Contract

> Canonical identity and certification rules: [`RELEASE_V0_1_CONTRACT.md`](RELEASE_V0_1_CONTRACT.md).

> **Contratto di rilascio.** v0.1 NON significa "finito": significa **prodotto
> utilizzabile con un contratto definito**. Le righe CERTIFIED hanno evidenza
> osservabile nel repo (test, gate, probe, benchmark); le righe PARTIAL sono
> dichiarate onestamente, senza fabbricazione di stato (AGENTS.md §honesty).

| | |
|---|---|
| **Tag** | `v0.1` |
| **Commit** | `7e86278e5535b799ec5c54960e520ce38c77244a` |
| **Certification status** | `BLOCKED / NOT CERTIFIED` on this SHA |
| **Data** | 2026-08-28 |
| **Versione CMake** | 0.1.0 |
| **Baseline verde storica** | `main@7eb5c2ba` 11/11 PASS (2026-07-06), non same-SHA |

## Contratto v0.1

Un consumatore può:

1. descrivere una composizione con **RenderPlan** (`render --plan`) o API SDK;
2. renderizzarla con il **software reference** (CPU, deterministica) o con il
   **path Vulkan** (GPU) quando disponibile;
3. comporre **text overlay**, **watermark/logo** e **subtitles**;
4. esportare **video** (MP4/H.264) via **NVENC** su hardware NVIDIA o codec CPU
   altrove;
5. ottenere output **deterministico** (stesso SHA per stessa scena/config);
6. servirsi del **daemon persistente** per job ripetuti a caldo.

## CERTIFIED

| # | Capacità | Evidenza |
|---|---|---|
| 1 | **RenderPlan** | `PreparedRenderPlan → CompiledComposition` + `render --plan` canonico; CLI V3 / RenderPlan PASS (CURRENT_STATUS §Stato per area) |
| 2 | **Software reference** | Renderer software CPU; determinismo seriale/parallelo + cold/warm SHA identici; baseline `main@7eb5c2ba` 11/11 PASS |
| 3 | **Vulkan path** | Backend Vulkan nativo compilato (`chronon3d_backend_vulkan`); Impl splittato in TU indipendenti; probe `CUDA_VULKAN_INTEROP_PASS` su NVIDIA RTX A4000; architecture gate 26/26 PASS; 4.11s GPU-native 1920x1080 960f watermark+subtitle con `gpu_readback_bytes=0`, `fallback=0`, `effective_backend=vulkan` |
| 4 | **Text overlay** | Text Core V1 PASS (FreeType/HarfBuzz/FriBidi + layout/cache/animator); 20+8 preset; 192/192 subtitle PASS |
| 5 | **Watermark/logo** | Layer immagine (`layer.image("logo", asset(...))`) + watermark nel RenderPlan (`examples/render_plan_video_native_smoke.json`); overlay cert fixture coverage |
| 6 | **Subtitles** | 192/192 subtitle PASS; word-timing JSON/SRT; preset subtitle 8 |
| 7 | **Video export** | Video pipeline PASS (13 codici errore, atomic output, 98 test); pipe export writer + timing sidecar |
| 8 | **NVENC** | `h264_nvenc`/`hevc_nvenc` wired (`native_av_encoder.cpp`, `ffmpeg_pipe_args.cpp`); probe `CUDA_VULKAN_NVENC_PASS: Vulkan image -> CUDA frame -> h264_nvenc`; NVENC-only-native benchmark corpus B01 |
| 9 | **Deterministic core** | Determinism PASS (seriale/parallelo + cold/warm SHA identici); `tools/check_determinism.sh` GATE-WIRED + selftest PASS |
| 10 | **Persistent daemon** | `chronon3d_daemon` (ADR-024 Level 1) + `DaemonService` warm render shell; daemon caldo 10.09s vs CLI 13.17s; 100 job × 150f stabili 1059s |

## PARTIAL (dichiarati, NON certificati)

| # | Capacità | Stato onesto |
|---|---|---|
| 1 | **Camera advanced** | Camera V1 PARTIAL: 9/9 visual + 64/64 suite + 406/406 scene PASS, ma manca baseline same-SHA + sanitizer |
| 2 | **Zero-copy complete** | GPU Production V1 PARTIAL: D2D Vulkan→CUDA→NVENC residua (`gpu_surface_copy_frames` + `encoder_staging_copy_bytes` ≠ 0); gate 3/4/6 strumentati 2026-08-28, percorso direct-YUV non ancora attivo |
| 3 | **Multi-GPU** | Non implementato (scheduler device-aware presente, ma nessun rendering multi-GPU attivo) |
| 4 | **HDR** | Non implementato (nessuna pipeline HDR certificata) |
| 5 | **CopyGop** | Video compiler arch V1 P2 PLANNED (`TICKET-VIDEO-COMPILER-ARCH-V1`); `gop_smart_copy` helper presente, GOP-copy end-to-end non certificato |

## Cosa NON è v0.1

- Non è una release con baseline verde same-SHA su HEAD (ultima baseline
  certificata: `main@7eb5c2ba`; HEAD con worktree sporco e CTest fast 92 test
  senza binari sul VPS).
- Non è zero-copy end-to-end (i 3 gate residui lo dimostrano).
- Non è parità completa con CapCut/After Effects/Remotion (milestone M3+).
- Le righe PARTIAL sono debito dichiarato, non feature nascoste: il prossimo
  ciclo di lavoro le consuma in ordine di priorità.

## Verdetto

**v0.1 = prodotto utilizzabile con contratto definito.** I 10 item CERTIFIED
costituiscono il contratto consumabile; i 5 PARTIAL sono il perimetro esplicito
di ciò che v0.1 non promette. Il tag `v0.1` marca questo contratto, non uno
stato di perfezione.

## Cross-link

- `docs/RELEASE_V0_1_CONTRACT.md` — contract canonico, identità e verdetto
- `docs/RELEASE_GATE.md` — requisiti release (baseline verde 11-gate)
- `docs/CURRENT_STATUS.md` — stato corrente per area
- `docs/ROADMAP.md` — milestone (V0.1 acceptance suite REGISTERED)
- `docs/ZERO_COPY_GATES.md` — stato 8 gate zero-copy
- `docs/baselines/main-7eb5c2ba-baseline.md` — baseline verde certificata
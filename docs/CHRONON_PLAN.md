# Chronon — Next Steps

> `AGENTS.md` vieta `NEXT_STEPS.md` come nome, questo è il doc operativo sostitutivo (`CHRONON_PLAN.md`).
> Stato corrente: [`CURRENT_STATUS.md`](CURRENT_STATUS.md) — baseline verde `main@7eb5c2ba` 11/11 (2026-07-06), HEAD `main@8aad8e00f` sporco.
> Requisiti release: [`RELEASE_GATE.md`](RELEASE_GATE.md).

## Principio

Chronon non è più CPU-first. Dopo `M4 Vulkan` è **hybrid GPU-accelerated, CPU oracle**. `README.md:3` e `AGENTS.md:88` vanno aggiornati. Ogni step sotto deve chiudere con **stesso-SHA baseline verde** (`verify_chronon_product_linux.sh` → `CHRONON_PRODUCT_FUNCTIONAL_PASS`), non con harness `WIRED`.

## Fase 0 — Stabilizzazione (1-2 settimane, blocca tutto)

Obiettivo: HEAD verde.

1. **Pulisci worktree** — 25 file modificati (`render_plan`, `gpu_glyph_atlas`, `text_batch.comp`, `command_plan_executor`). Commit atomici + `bash tools/wrap_push.sh origin main`.
2. **Fix P0** — `schemas/chronon.render-plan.v1.schema.json:14` `fps_num/fps_den` fallback in `src/render_plan/render_plan_decoder.cpp:9`; `src/runtime/gpu_glyph_atlas.cpp:68` ref invalid; `include/chronon3d/runtime/gpu_glyph_atlas.hpp:48` hash incompleto; `src/backends/vulkan/vulkan_backend_impl_ops.inc:401` multi-page ignorata, `491` FNV → `XXH3_64` (hai già `xxHash`).
3. **WBH run** — su RTX A4000 + CUDA 13: `tools/run_cuda_vulkan_external_memory_probe.sh → CUDA_VULKAN_INTEROP_PASS`, `tools/verify_chronon_product_linux.sh` 15/15, `tools/verify_performance_linux.sh` 5/5 + leak ≤10%. Committa `docs/baselines/main-<sha>-baseline.md` + `bench/baselines/main-<sha>-bench.json` con p50/p95 reali.
4. **Docs** — aggiorna `README.md` + `AGENTS.md` hybrid, svuota `CURRENT_STATUS.md` a <100 righe (fatto), tieni solo `docs/baselines/main-7eb5c2ba-baseline.md` come riferimento.

Gate: `bash tools/check_architecture.py` 26/26 + `ctest --preset linux-ci` verde stesso SHA.

## Fase 1 — CPU executor (2-3 settimane, 3-7x su scene cinematic)

1. **Invisible-layer skip** `src/render_graph/executor/executor_levels.cpp:36` popola `resolved_opacity = node.opacity_anim().evaluate(FrameContext{frame,fps})`, `src/render_graph/executor/node_runner.cpp:308` `if(opacity<0.001 && !has_effects) cull`. WhipPan `200→26ms` `docs/archive_docs/PERFORMANCE_BOTTLENECKS.md:178`.
2. **Frame parallelism** `tbb::parallel_for` su chunk 4 frame con `FramebufferPool` shardato `src/runtime/cpu_budget.hpp:3` + `apps/chronon3d_cli/utils/job/render_job_loop.cpp:54`. Sharda PMR arena per livello `executor_levels.cpp:41` (thread_local `monotonic_buffer_resource`). `2x` throughput.
3. **Double-buffer render/encode** `SPSCQueue<OwnedFB,2>` in `CpuBudget` `src/core/cpu_budget.cpp:54` — `1.3-1.5x` su 4K60. Misura con `tools/measure_cpu_budget.sh`.
4. **Soglie TBB** `software_compositor.cpp:202` `8→16`, `clear_node.cpp:145` `64→32`, `transform_kernels.cpp:75` `simple_partitioner grainsize 128`.

## Fase 2 — GPU zero-copy (2 settimane, chiude +1.58s)

1. **Surface importabile** — `src/media/video/raw_video_sink.cpp:286` alloca `AVFrame nv12` con `VK_EXTERNAL_MEMORY_FD + VK_EXTERNAL_SEMAPHORE_FD`, esporta `vkGetMemoryFdKHR`, importa `cudaImportExternalMemory` `src/backends/vulkan/cuda_vulkan_surface_bridge.cpp:57`. Abilita `VK_KHR_external_memory_fd` in `src/backends/vulkan/vulkan_backend.cpp:299`. Fix `hwmap=derive_device=vulkan` `CURRENT_STATUS.md:60`. Target `encoder_staging_copy_bytes=0`.
2. **Glow fusion** — unico `glow.comp` invece di `blurH+blurV+composite` 3 dispatch `vulkan_backend_impl_ops.inc:197` → 1 barrier. -16 barrier/frame.
3. **Cull off-screen** — `node_runner.cpp:113` `if(opacity<=0.001) continue` + `vulkan_backend_impl_ops.inc:436` bounds check `x1<=0|x0>=W` evita upload + full-screen fallback.
4. **Scale kernel reale** — `command_plan_executor.cpp:85` `dispatch_scale` `scale.comp` invece di finto `xform{1/sx}` → `transform_surface_affine` legacy.

## Fase 3 — Pipeline & I/O (1 settimana)

1. **Tile dirty 64x64** `src/render_graph/tile_pruning.hpp` bitmask invece di union rect `node_runner.cpp:518` — `dirty 105%→60%`.
2. **Clear policy** esplicita al call site `src/render_graph/framebuffer_acquire.cpp:12`, evita `clear_node.cpp:331` su nodi skippati.
3. **Remux chunked** `apps/chronon3d_cli/commands/video/exporters/video_export_chunked.cpp:435` — `avformat_write_header` 1 volta + `pts_offset`, copia `extradata`.

## Fase 4 — Build & SIMD (3 giorni, 10-15% gratis)

1. **PGO+ThinLTO** `cmake/presets/optimizations.json` `release-pgo-thinlto` `-flto=thin`, tieni `-ffp-contract=off` `CMakeLists.txt:19` per determinismo.
2. **SIMD integer unpack** `src/backends/software/highway_color_kernels.cpp` `hwy::ShiftRight+And+ConvertTo` invece di `>>24 &0xFF /255` scalare — `1.5-2.7x`.
3. **Rimuovi dead shader** `text_tile_bin/raster.comp` da `src/backends/vulkan/CMakeLists.txt:23` o cablali.

## Fase 5 — Prodotto

1. **Benchmark ufficiale** — promuovi `TICKET-BENCHMARK-CORPUS-OFFICIAL` + `TICKET-P1E-CPU-BUDGET`: versiona `bench/baselines/*.json` con `tools/validate_benchmark_json.sh`, `bench/run_perf_bench.sh` → `corpus_v1.json` B00-B11, pubblica `docs/baselines/bench-corpus-v1-p50p95.csv`.
2. **Node memory metrics** — `TICKET-NODE-MEMORY-METRICS` 8 campi + `tools/check_node_memory_metrics.sh` per intercettare `vector` alloc per-frame `node_runner.cpp:75`.
3. **CapCut parity** — corpus PNG `TICKET-CAPCUT-REFERENCE-CORPUS` (blocco Text Production V1).

## Cosa NON fare

- Nuovi effetti/preset/binding/plugin prima di Fase 0 verde (ordine `ROADMAP.md:30`).
- Nuovi singleton/registry/cache senza ADR (`AGENTS.md:84`, `tools/architecture_rules.toml`).
- `#include <msdfgen>/<libtess2>/<unicode>` senza ADR.
- Percentuali manuali (`AGENTS.md:85`) — solo `PASS/FAIL/PARTIAL/NOT RUN`.

## Ordine con gain atteso

`Fase0 → A1 (7x) → B1 (1.58s) → A3 (1.4x video) → D1 (10%) → B2+C1` = `4.11s → ~2.7s` poi sotto `2.53s` NVDEC→NVENC.

## Verifica per ogni commit

```bash
bash tools/check_architecture.py
ctest -R "chronon3d_tests_fast|backend_registry|compositor|render_graph" --output-on-failure
bash tools/verify_performance_linux.sh
bash tools/run_cuda_vulkan_external_memory_probe.sh # su RTX A4000
```

Archivio pulizia: `docs/CHANGELOG.archive.md`, `docs/ROADMAP.archive.md`, `docs/tickets/archive/` (113 ticket), `docs/baselines/archive/` (17 baselines), `docs/archive_docs/` (17 doc duplicati). Backup in `/tmp/chronon_cleanup_backup/`.

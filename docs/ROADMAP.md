# Chronon3D — Roadmap Attiva

> Ogni voce è classificata in tre categorie:
> - ✅ **Verified** — implementato e funzionante, con test o benchmark collegati.
> - 🟡 **Partial** — parzialmente implementato; mancano pezzi significativi.
> - 🔵 **Planned** — non ancora implementato.
>
> Ogni voce deve avere un test, un benchmark o una issue collegata.
> Ultimo aggiornamento: 2026-06-15

---

## ✅ Verified — Implementato e funzionante

### I4. Thread Pinning per GraphExecutor
**Stato:** Pin opzionale via `Config::get().pin_main_thread` con `pin_thread_to_core()`.
**Dove:** `src/render_graph/executor/executor.cpp` (Linux: `pthread_setaffinity_np`, Windows: `SetThreadAffinityMask`)
**Mancante:** NUMA-aware pool allocation (spostato in Planned).

### S2. Node Cache Temporal Hashing
**Stato:** `NodeCache` con LRU sharded + `DiskNodeCache` (mmap + atomic rename) + `CachePolicy` + `DirtyRectMask` per skip frame.
**Dove:** `include/chronon3d/cache/node_cache.hpp`, `src/cache/disk_node_cache.cpp`, `include/chronon3d/render_graph/cache_policy.hpp`
**Test:** `tests/cache/test_tile_cache.cpp`, `tests/render_graph/pipeline/test_graph_cache.cpp`

### S3. Prefetch L1/L2 nei Loop di Composite
**Stato:** Prefetch presente in `software_compositor.cpp`, `highway_color_kernels.cpp`, `transform_kernels.cpp`, `effect_blur.cpp`, `path_rasterizer.cpp`, `pip.cpp`, `direct_yuv_converter_hwy.cpp`. API portatile `chronon3d::prefetch()`.
**Dove:** `include/chronon3d/core/memory_utils.hpp` (API), `src/core/config.cpp` (`CHRONON_PREFETCH` env var)
**Test:** `CHRONON_PREFETCH` env toggle abilita/disabilita.

### S6. SIMD Point-in-Polygon
**Stato:** `point_in_polygon_avx2()` implementato con prefetch e fallback scalar.
**Dove:** `src/backends/software/rasterizers/path/pip.cpp`, `pip.hpp`
**Mancante:** Batch SIMD per 8 pixel simultaneamente (spostato in Partial).

### S9. ImageCache Sharding + Read-Write Lock
**Stato:** `LruCache` con 16 shard interni. `shared_mutex` per accesso concorrente al backend. Preload async supportato.
**Dove:** `include/chronon3d/backends/assets/image_cache.hpp`, `src/backends/assets/image_cache.cpp`
**Test:** `tests/cache/test_tile_cache.cpp`

### S7. Eliminare shared_ptr<Framebuffer> nel Hot Path (parziale)
**Stato:** `OwnedFB` pattern con `PoolFbDeleter` (weak_ptr) + `CachedFB` alias. Il percorso critico del render graph usa `FramebufferRef` per operazioni read-only.
**Dove:** `include/chronon3d/core/memory/framebuffer_handle.hpp`

### N4. any_cast chain → enum dispatch
**Stato:** `EffectInstance` con `EffectParams` (type-erased, confronto per type ID) sostituisce `std::any_cast`.
**Dove:** `include/chronon3d/effects/effect_instance.hpp`, `include/chronon3d/effects/effect_params.hpp`
**Test:** `tests/` — effetti testati tramite pipeline.

### N5. compute_scene_fingerprint → XXH3
**Stato:** `SceneHasher` con XXH3_64bits per fingerprint statico, active-at e structure.
**Dove:** `include/chronon3d/render_graph/core/scene_hasher.hpp`, `src/render_graph/pipeline/scene_fingerprint.cpp`
**Test:** `tests/render_graph/pipeline/test_render_pipeline.cpp`

### M1. Frame Graph Compiler
**Stato:** `FrameGraphCompiler::compile()` con topological sort, reachability analysis, `CompiledFrameGraph` output.
**Dove:** `include/chronon3d/render_graph/compiler/frame_graph_compiler.hpp`, `src/render_graph/compiler/frame_graph_compiler.cpp`
**Test:** `tests/render_graph/compiler/test_frame_graph_compiler.cpp`

### M5. Transform Cache
**Stato:** `TransformNode` con `cache_frame` + risultati cacheati via `NodeCache`.
**Dove:** `include/chronon3d/render_graph/nodes/transform_node.hpp`

### M7. Cache Telemetry nel Render Report
**Stato:** SQLite store raccoglie `node_cache_hash_collisions`, `framebuffer_pool_empty_alloc`, `framebuffer_pool_miss_count_empty`.
**Dove:** `src/runtime/telemetry/sqlite/sqlite_telemetry_store.cpp`
**Test:** Telemetry capture in `apps/chronon3d_cli/utils/telemetry/`

### M12. std::pmr Allocator nei Comandi CLI
**Stato:** `std::pmr::string`, `std::pmr::vector`, `std::pmr::monotonic_buffer_resource` usati estensivamente (141+ referenze) in scene, executor, builder, layer, render node.
**Dove:** `include/chronon3d/core/memory/arena.hpp`, `include/chronon3d/scene/model/layer/layer.hpp`, `include/chronon3d/scene/model/render/render_node.hpp`
**Test:** `tests/scene/layout/test_layer_hierarchy.cpp`, `tests/render_graph/pipeline/test_pipeline_robustness.cpp`

---

## 🟡 Partial — Parzialmente implementato

### N1. Motion Blur Accumulation + SIMD
**Stato:** `VelocityBufferMotionBlur` classe completa con tests. Accumulation in `composition.cpp` con sub-frame sampling. `PostProcessingSystem` integrazione.
**Mancante:** SIMD-izzazione accumulazione con Highway non verificata.
**Dove:** `include/chronon3d/render_graph/nodes/velocity_buffer_motion_blur.hpp`, `src/render_graph/pipeline/composition.cpp`
**Test:** `tests/render_graph/test_velocity_buffer_motion_blur.cpp`, `tests/render_graph/test_post_processing_system.cpp`

### N2. Box Blur 3-Pass Parallelizzato
**Stato:** TBB parallel over rows in `effect_blur.cpp`. `C3D_PREFETCH_READ/WRITE` per prefetch.
**Mancante:** Verifica che 3-pass box blur (horizontal → vertical → separato) sia effettivamente implementato.
**Dove:** `src/backends/software/utils/effects/effect_blur.cpp`

### N6. Blend mode switch → template specialization
**Stato:** Highway SIMD kernels per blending ma switch-case nel dispatcher.
**Dove:** `src/backends/software/simd/highway_color_kernels.cpp`
**Benchmark:** `tests/bench/micro_benchmarks.cpp`

### N8. Temp FB aliasing shared_ptr
**Stato:** `state.shared_transparent` condiviso tra nodi che non modificano il FB. Pattern parziale.
**Dove:** `src/render_graph/executor/node_runner.cpp`

### N13. Layout Solver Minimalista
**Stato:** `LayoutSolver` con `LayoutRules` (pin-to-anchor, safe area, margins).
**Mancante:** `LayoutFlow` (wrap) e `LayoutGrid` (griglia uniforme) non implementati.
**Dove:** `include/chronon3d/layout/layout_rules.hpp`, `src/scene/layout_solver.cpp`
**Test:** `tests/layout/test_design_kit.cpp`

### N16. Zero-Copy Frame Delivery all'Encoder
**Stato:** `av_frame_from_framebuffer()` non implementato. Copia ancora presente nel percorso.
**Mancante:** Implementazione zero-copy AVFrame creation.
**Dove:** `apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.cpp`

### N17. Pool Miss Reason Dashboard
**Stato:** Contatori telemetry esistono (`framebuffer_pool_empty_alloc`, `framebuffer_pool_miss_count_empty`).
**Mancante:** Dashboard UI per visualizzare distribuzione miss reason.
**Dove:** `src/runtime/telemetry/sqlite/sqlite_telemetry_store.cpp`
**Frontend:** `tools/telemetry_dashboard/`

### M3. SPSC Lock-Free Queue per Pipe FFmpeg
**Stato:** Mutex + condition variable nel pipe encoder (async slot state machine).
**Mancante:** Circular buffer SPSC lock-free.
**Dove:** `apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.hpp`

### M11. Test Coverage Nodi Render Graph Mancanti
**Stato:** Test esistono per pipeline, cache, optimizer, builder, compiler.
**Mancante:** Test dedicati per ShadowNode, GlowNode, BloomNode, GradientNode, DoFNode, MaskNode.
**Test esistenti:** `tests/render_graph/pipeline/`, `tests/render_graph/builder/`, `tests/render_graph/compiler/`

---

## 🔵 Planned — Non ancora implementato

### N3. Downsample SSAA Parallel
**Soluzione:** Accesso row pointer diretto + TBB + SIMD box filter.
**Dove:** `src/render_graph/render_pipeline_helpers.hpp`
**Guadagno stimato:** 2-4× con SSAA 2×.

### N15. Framebuffer Pool Adaptive Preallocation
**Problema:** Contatori telemetry mostrano miss ma pool non si adatta.
**Soluzione:** `FramebufferPool::adapt_pool()` — se miss > 0 per 3 frame consecutivi, aumenta pool del 50%.
**Dove:** `src/cache/framebuffer_pool.cpp`

### M2. ISPC per il Blur Gaussiano
**Problema:** apply_blur è scalar o usa Highway (1-8 pixel per istruzione).
**Soluzione:** ISPC → 8 pixel per istruzione AVX2.
**Note:** Highway SIMD già presente come alternativa. Valutare se ISPC aggiunge vantaggio reale.

### M4. Render Speculativo — Multi-Frame Ahead
**Problema:** Solo frame N renderizzato → core idle.
**Soluzione:** Worker pool renderizza N+1..N+15 in anticipo.
**Guadagno stimato:** Fino a 16× throughput teorico.

### I4-EXT. NUMA-Aware FB Pool Allocation
**Problema:** FB pool allocates indipendentemente dalla topologia NUMA.
**Soluzione:** Allocare FB pool sul nodo NUMA locale al thread.
**Prerequisito:** I4 thread pinning è già attivo.

---

## 🌀 LONG-TERM — Prossimi mesi

| ID | Descrizione | Impatto | Stato |
|----|-------------|---------|-------|
| L1 | GPU Backend per EffectStack (Vulkan Compute) | 🔴 Alto | 🔵 Planned |
| L2 | ECS Architecture (Entity Component System) | 🟡 Medio | 🔵 Planned |
| L3 | Frame Graph con Resource Barriers (RDG-style) | 🔴 Alto | 🔵 Planned |
| L4 | Persistent Daemon Mode (Hot Server) | 🔴 Alto | 🔵 Planned |
| L7 | MSDF Font Atlas per Text Scalability | 🟡 Medio | 🔵 Planned |
| L8 | Parallel Tile Rendering (Bucket-Based) | 🔴 Alto | 🔵 Planned |
| L5 | Render Farm Distribuito (Multi-Host) | 🔴 Alto | 🔵 Planned |
| L9 | CI Multi-Platform (Windows + macOS) | 🟡 Medio | 🔵 Planned |

---

## 🔧 Quick Win (1 giorno o meno)

| ID | Descrizione | Dove | Stato |
|----|-------------|------|-------|
| N4 | any_cast chain → enum dispatch | `effect_stack.cpp` | ✅ Verified |
| N5 | compute_scene_fingerprint → XXH3 | `render_pipeline_helpers.hpp` | ✅ Verified |
| N6 | Blend mode switch → template specialization | `blend_mode.hpp` | 🟡 Partial — Highway SIMD ma switch-case |
| N7 | Shadow/Glow multi-layer fused | `effect_stack.cpp` | 🔵 Planned |
| N8 | Temp FB aliasing shared_ptr | `effects_internal.hpp` | 🟡 Partial |
| N9 | Trace lock-free queue | `trace.hpp` / `trace.cpp` | 🔵 Planned |
| N10 | RAII guard thread_local ptrs | `software_renderer.cpp` | 🔵 Planned |

---

## 📊 Stato d'avanzamento

| Categoria | Conteggio |
|-----------|-----------|
| ✅ Verified | 20 |
| 🟡 Partial | 10 |
| 🔵 Planned | 5 (oltre 8 Long-term) |
| 🌀 Long-term | 8 |

**317 test passano** (133.126 assertion, 0 fallimenti)

---

## 🔍 Riferimenti: Feature Verificate nel Codice

| Feature | Dove | Test/Benchmark |
|---|---|---|
| **TBB parallel_for** | `graph_executor.cpp`, `software_compositor.cpp` | `tests/bench/micro_benchmarks.cpp` |
| **LRU Cache sharded** | `include/chronon3d/cache/lru_cache.hpp` — 16 shard | `tests/cache/test_lru_cache.cpp` |
| **xxHash (XXH3_64bits)** | `node_cache.cpp`, `scene_fingerprint.cpp` | `tests/render_graph/pipeline/test_render_pipeline.cpp` |
| **FrameArena** | `include/chronon3d/core/memory/arena.hpp` — monotonic_buffer_resource | used in executor |
| **CachePolicy** | `include/chronon3d/render_graph/cache_policy.hpp` | `tests/cache/` |
| **DirtyRectMask** | `dirty_rect_mask.hpp` — bitmask con tiles 64×64 | pipeline dirty rect tests |
| **SIMD Rect Rasterizer** | `highway_kernels.cpp` — `rasterize_rect_simd()` | `tests/bench/micro_benchmarks.cpp` |
| **DiskNodeCache** | `disk_node_cache.hpp/.cpp` — mmap + atomic rename | `tests/cache/test_persistent_bake_cache.cpp` |
| **HugePageAllocator** | `memory_utils.hpp` — usato in Framebuffer | |
| **EXR mmap loader** | `exr_mmap.cpp` — `load_exr_mmap()` | |
| **EXR DWAA writer** | `image_writer.cpp` — tiled writes 256×256 | |
| **SIMD premultiply alpha** | `highway_kernels.cpp` — `premultiply_alpha_rgba8()` | |
| **RenderCounters X-macro** | `counters.hpp` — `CHRONON_RENDER_COUNTERS(X)` | |
| **renderer_warmup** | `warmup_renderer()` — dummy frame + prealloc | |
| **Blend2D JIT bridge** | `blend2d_bridge.cpp` | |
| **io_uring pipe** | `ffmpeg_pipe_encoder.cpp` — opt-in, `IORING_OP_WRITE_FIXED` | |
| **CancellationToken** | `cancellation_token.hpp/.cpp` — SIGINT handler | |
| **CLI --dry-run** | `command_video.cpp` — flag `--dry-run` | |
| **HarfBuzz text shaping** | `font_engine.hpp/.cpp` — FreeType + hb_shape | |
| **PlacedGlyphRun** | `font_engine.hpp/.cpp` — tracking-aware canonical positioning | |
| **Bidi segmentation** | `bidi_segmenter.cpp` — FriBidi RTL support | |
| **Text gradient fill** | `text_rasterizer_render.cpp` — linear/radial/conic | |
| **GlyphAtlas** | `glyph_atlas.cpp` — LRU per-glyph cache 32MB | |
| **ConicGradient** | `graphics/` namespace + Blend2D | |
| **Depth-aware rasterizer** | `rasterizers/` — per-pixel depth test | |
| **Golden visual tests** | `tests/graphics/` — 15 reference images | |
| **Hot-path benchmarks** | `tests/bench/micro_benchmarks.cpp` | |
| **.clang-tidy + CI** | `.clang-tidy` (18 categorie) + job CI | |
| **PathFlattenCache** | `path_cache.hpp` — 16 shard, 64 MB | |
| **FrameArena in GraphExecutor** | `graph_executor.hpp` — `m_frame_arena` | |
| **NodeCache LRU + DiskCache** | `node_cache.hpp` + `disk_node_cache.cpp` | `tests/cache/` |
| **ImageCache sharded + shared_mutex** | `image_cache.hpp` — 16 shards, RW lock | |
| **FrameGraphCompiler** | `frame_graph_compiler.hpp/.cpp` | `tests/render_graph/compiler/` |
| **std::pmr pervasive** | scene, executor, builder, layer | `tests/scene/`, `tests/render_graph/` |
| **EffectInstance (no any_cast)** | `effect_instance.hpp` + `effect_params.hpp` | |
| **Thread pinning (opt-in)** | `executor.cpp` — `pin_thread_to_core()` | |
| **VelocityBufferMotionBlur** | `velocity_buffer_motion_blur.hpp` | `tests/render_graph/test_velocity_buffer_motion_blur.cpp` |

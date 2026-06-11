# Chronon3D — Roadmap Attiva

> Item ancora da implementare, ordinati per priorità.
> Estratti dalla vecchia `IMPROVEMENTS.md`.

---

## 🚨 IMMEDIATE — 1-2 ore ciascuno

### I4. Thread Affinity per TBB Arena + NUMA
**Problema:** I thread TBB vagano liberamente tra core → cache L1/L2 non hot, NUMA cross-socket.
**Soluzione:** Pin dei thread ai core fisici nel costruttore di `GraphExecutor`.
**Dove:** `src/render_graph/graph_executor.cpp`
**Guadagno stimato:** 5-10% throughput su multi-socket.
- [ ] Helper `pin_current_thread_to_core(core_id)`
- [ ] `SetThreadAffinityMask` / `sched_setaffinity` in execute()
- [ ] Allocare FB pool sul nodo NUMA locale

---

## ⚡ SHORT-TERM — Questa settimana

### N2. Box Blur 3-Pass Parallelizzato
**Problema:** I loop di riga/colonna nel blur sono seriali.
**Soluzione:** Parallelizzare ogni pass con `tbb::parallel_for`.
**Dove:** `src/backends/software/utils/effects/effect_blur.cpp`
**Guadagno stimato:** 4-8× speedup su raggi grandi (50+).

### N1. Motion Blur Accumulation Parallel + SIMD
**Problema:** Gli N samples di motion blur sono valutati sequenzialmente.
**Soluzione:** Parallelizzare con TBB + SIMD-izzare accumulazione con Highway.
**Dove:** `src/render_graph/render_pipeline_composition.cpp`
**Guadagno stimato:** 4-8× speedup con 8 samples.

### S2. Temporal Hashing — Cache Nodo con Storia
**Problema:** Nodo ricalcolato anche se input invariati tra frame consecutivi.
**Soluzione:** Hash del nodo + history hash. Cache temporale.
**Dove:** `include/chronon3d/render_graph.hpp`
**Guadagno stimato:** Skip completo render per animazioni lineari.

### S3. Prefetch L1/L2 nei Loop di Composite
**Problema:** Loop di blending ha cache miss.
**Soluzione:** `_mm_prefetch` ogni 16 pixel.
**Dove:** `src/backends/software/simd/highway_kernels.cpp`
**Guadagno stimato:** ~3-5% IPC su Zen3.

**STATO: 🟡 Parzialmente implementato** — il prefetch è già presente in:
- `highway_color_kernels.cpp` (color conversion loop)
- `transform_kernels.cpp` (bilinear sampling)
- `effect_blur.cpp` (blur loop — C3D_PREFETCH_READ/WRITE)
- `path_rasterizer.cpp` + `pip.cpp` (path raster hot loop)
- API portatile in `include/chronon3d/core/memory_utils.hpp`

**Mancante:**
- Compositing puro (`software_compositor.cpp`): non ha prefetch esplicito
- Benchmark dedicati: mancano misurazioni IPC/cache-miss con `perf stat`
- Prefetch abilitabile via `CHRONON_PREFETCH` env var (default true)

### S6. SIMD Point-in-Polygon per Path Rasterizer
**Problema:** `point_in_polygon_even_odd()` chiamato per ogni pixel dentro la bbox. Interamente scalar.
**Soluzione:** Batch processing — per 8 pixel contemporaneamente con SIMD.
**Dove:** `src/backends/software/rasterizers/path_rasterizer.cpp`
**Guadagno stimato:** 3-5× speedup se è il collo di bottiglia.

### S9. ImageCache Sharding + Read-Write Lock
**Problema:** `ImageCache` usa un singolo `std::mutex` — contention durante preload multi-thread.
**Soluzione:** Cache sharded con `std::shared_mutex` (pattern `LruCache` già esistente).
**Dove:** `include/chronon3d/backends/assets/image_cache.hpp` + `image_cache.cpp`
**Guadagno stimato:** Preload parallelo 2-3× più veloce.

### S7. Eliminare shared_ptr<Framebuffer> nel Hot Path
**Problema:** 162+ posizioni con shared_ptr → atomic reference count overhead.
**Soluzione:** Usare `Framebuffer*` raw pointers.
**Dove:** `include/chronon3d/render_graph/nodes/*.hpp` + `graph_executor.cpp`
**Guadagno stimato:** -3-5% frame time.

### N3. Downsample SSAA Parallel
**Soluzione:** Accesso row pointer diretto + TBB + SIMD box filter.
**Dove:** `src/render_graph/render_pipeline_helpers.hpp`
**Guadagno stimato:** 2-4× con SSAA 2×.

### N13. Layout Solver Minimalista
**Problema:** Layout solver supporta solo pin-to-anchor e safe area.
**Soluzione:** Implementare `LayoutFlow` (wrap) e `LayoutGrid` (griglia uniforme).
**Dove:** `src/layout/layout_solver.cpp` + `include/chronon3d/layout/layout_rules.hpp`
**Guadagno stimato:** Feature per composizioni complesse.

### N15. Framebuffer Pool Adaptive Preallocation
**Problema:** I nuovi counter telemetry (`framebuffer_acquire_ms`, `pool_miss_count_empty`) mostrano il problema ma il pool non si adatta.
**Soluzione:** `FramebufferPool::adapt_pool()` — se miss > 0 per 3 frame consecutivi, aumenta pool del 50%.
**Dove:** `src/cache/framebuffer_pool.cpp`
**Guadagno stimato:** Pool self-tuning, zero configurazione manuale.

### N16. Zero-Copy Frame Delivery all'Encoder
**Problema:** `frame_conversion_copy_ms` mostra copie costose prima di FFmpeg.
**Soluzione:** `av_frame_from_framebuffer()` — crea AVFrame senza copiare dati.
**Dove:** `apps/chronon3d_cli/utils/ffmpeg_pipe_encoder.cpp`
**Guadagno stimato:** -30-50% di frame_conversion_copy_ms.

### N17. Pool Miss Reason Dashboard
**Problema:** Miss reason counters esistono ma non c'è dashboard che li mostra.
**Soluzione:** Grafico a barre nel frontend React della distribuzione miss reason.
**Dove:** `tools/telemetry_dashboard/frontend/src/components/MetricsGrid.jsx`
**Guadagno stimato:** Visibilità immediata sul collo di bottiglia del pool.

---

## 🏗️ MEDIUM-TERM — Questo mese

### M1. Frame Graph Compiler — Render Graph to Executable
**Problema:** Ogni nodo è chiamata virtuale → 35+ virtual calls per frame.
**Soluzione:** Generare codice C++ lineare a tempo di build, compile con `/O2`.
**Guadagno stimato:** 30-40% speedup.

### M2. ISPC per il Blur Gaussiano
**Problema:** apply_blur è scalar — 1 pixel per istruzione.
**Soluzione:** Riscrivere in ISPC → 8 pixel per istruzione AVX2.
**Guadagno stimato:** ~3.5× su Zen4.

### M3. SPSC Lock-Free Queue per Pipe FFmpeg
**Problema:** Mutex + condition variable → context switch costosi.
**Soluzione:** Circular buffer SPSC lock-free.
**Guadagno stimato:** Zero latenza di sincronizzazione.

### M4. Render Speculativo — Multi-Frame Ahead
**Problema:** Solo frame N renderizzato → core idle.
**Soluzione:** Worker pool renderizza N+1..N+15 in anticipo.
**Guadagno stimato:** Fino a 16× throughput teorico.

### M5. Transform Cache
**Problema:** TransformNode ricalcola matrice ogni frame.
**Soluzione:** Cache risultati animazione baked.
**Guadagno stimato:** ~5-10% speedup per scene multi-layer.

### M7. Cache Telemetry nel Render Report
**Soluzione:** Aggregare cache stats (node, text, image) nel BenchmarkReport JSON.

### M11. Test Coverage Nodi Render Graph Mancanti
Test per: ShadowNode, GlowNode, BloomNode, GradientNode, DoFNode, MaskNode.

### M12. std::pmr Allocator nei Comandi CLI
Usare `std::pmr::string` / `std::pmr::vector` nei comandi CLI.

---

## 🌀 LONG-TERM — Prossimi mesi

| ID | Descrizione | Impatto |
|----|-------------|---------|
| L1 | GPU Backend per EffectStack (Vulkan Compute) | 🔴 Alto |
| L2 | ECS Architecture (Entity Component System) | 🟡 Medio |
| L3 | Frame Graph con Resource Barriers (RDG-style) | 🔴 Alto |
| L4 | Persistent Daemon Mode (Hot Server) | 🔴 Alto |
| L7 | MSDF Font Atlas per Text Scalability | 🟡 Medio |
| L8 | Parallel Tile Rendering (Bucket-Based) | 🔴 Alto |
| L5 | Render Farm Distribuito (Multi-Host) | 🔴 Alto |
| L9 | CI Multi-Platform (Windows + macOS) | 🟡 Medio |

---

## 🔧 Quick Win (1 giorno o meno)

| ID | Descrizione | Dove |
|----|-------------|------|
| N4 | any_cast chain → enum dispatch | `effect_stack.cpp` |
| N5 | compute_scene_fingerprint → XXH3 | `render_pipeline_helpers.hpp` |
| N6 | Blend mode switch → template specialization | `blend_mode.hpp` |
| N7 | Shadow/Glow multi-layer fused | `effect_stack.cpp` |
| N8 | Temp FB aliasing shared_ptr | `effects_internal.hpp` |
| N9 | Trace lock-free queue | `trace.hpp` / `trace.cpp` |
| N10 | RAII guard thread_local ptrs | `software_renderer.cpp` |

---

## 📊 Stato d'avanzamento

**Completati ✅:** 25+ item (vedi CHANGELOG.md)
**Attivi 📋:** 25+ item (questo documento)
**317 test passano** (133.126 assertion, 0 fallimenti)

---

## 🔍 Riferimenti: Cose Già Implementate

| Feature | Dove |
|---|---|
| **TBB parallel_for** | `graph_executor.cpp`, `software_compositor.cpp` |
| **LRU Cache sharded** | `include/chronon3d/cache/lru_cache.hpp` — 16 shard |
| **xxHash (XXH3_64bits)** | `node_cache.cpp`, `text_rasterizer_utils.cpp` |
| **FrameArena** | `include/chronon3d/core/arena.hpp` — monotonic_buffer_resource |
| **CachePolicy** | `include/chronon3d/render_graph/cache_policy.hpp` |
| **DirtyRectMask** | `dirty_rect_mask.hpp` — bitmask con tiles 64×64 |
| **SIMD Rect Rasterizer** | `highway_kernels.cpp` — `rasterize_rect_simd()` |
| **DiskNodeCache** | `disk_node_cache.hpp/.cpp` — mmap + atomic rename |
| **HugePageAllocator** | `memory_utils.hpp` — usato in Framebuffer |
| **EXR mmap loader** | `exr_mmap.cpp` — `load_exr_mmap()` |
| **EXR DWAA writer** | `image_writer.cpp` — tiled writes 256×256 |
| **SIMD premultiply alpha** | `highway_kernels.cpp` — `premultiply_alpha_rgba8()` |
| **RenderCounters X-macro** | `counters.hpp` — `CHRONON_RENDER_COUNTERS(X)` |
| **renderer_warmup** | `warmup_renderer()` — dummy frame + prealloc |
| **Blend2D JIT bridge** | `blend2d_bridge.cpp` |
| **io_uring pipe** | `ffmpeg_pipe_encoder.cpp` — `IORING_OP_WRITE_FIXED` |
| **CancellationToken** | `cancellation_token.hpp/.cpp` — SIGINT handler |
| **CLI --dry-run** | `command_video.cpp` — flag `--dry-run` |
| **HarfBuzz text shaping** | `font_engine.hpp/.cpp` — FreeType + hb_shape |
| **Hot-path benchmarks** | `tests/bench/micro_benchmarks.cpp` |
| **.clang-tidy + CI** | `.clang-tidy` (18 categorie) + job CI |
| **PathFlattenCache** | `path_cache.hpp` — 16 shard, 64 MB |
| **FrameArena in GraphExecutor** | `graph_executor.hpp` — `m_frame_arena` |

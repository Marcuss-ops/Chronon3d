# Chronon3D — Changelog

> Cronologia degli item completati, estratti dalla vecchia `IMPROVEMENTS.md`.

---

## Completati (Maggio-Giugno 2026)

### PRs 1-4 (Performance)

_Data: fine maggio 2026_

| PR | Ottimizzazione | Bench | Prima | Dopo | Speedup |
|----|----------------|-------|-------|------|---------|
| PR 1 | Skip-When-Opaque CompositeNode | ImgGridTest cold-start | 425ms | 303ms | 1.4× |
| PR 2 | Execution plan cache | TextGridOverlay (164 nodi) | N/A | skip hash O(164) | — |
| PR 3 | Static fingerprint fast-path | ImgGridTest | 311ms | **0.42ms** | **740×** |
| PR 3 | Static fingerprint | DarkGridBackground | 3.88ms | **0.34ms** | **11×** |
| PR 4 | Graph structure hint executor | skip compute_structure_signature | O(n) per frame | ~0 quando invariato | — |

### I1 — Bake EXR + mmap per grid_bg

2026-05-23 — `exr_mmap.cpp` con `load_exr_mmap()`, `DarkGridBackground` usa EXR con DWAA compression e half-float, `command_bake_layer` supporta `--exr-bake` flag.

### I2 — Huge Pages per FramebufferPool

2026-05-23 (e57af11) — Helper `allocate_huge_pages()` / `free_huge_pages()` in `memory_utils.hpp`, `HugePageAllocator<T>` allocatore STL-compatibile, integrato in `Framebuffer` via `HugePageAllocator<Color>`.

### I3 — Dirty Rect Bitmask per Compositing

2026-06-07/10 — `DirtyRectMask` definito in `include/chronon3d/core/dirty_rect_mask.hpp`, integrato in `RenderNode` e usato nel compositing.

### I5 — FrameArena nel Render Pipeline

2026-05-12/22 — `FrameArena` membro di `GraphExecutor` (`m_frame_arena`), usata in `graph_executor_phases.cpp` per allocazioni temporanee, con guard RAII per reset a fine frame.

### I6 — DiskNodeCache per Nodi Statici

2026-05-23 (759b1dc) — `DiskNodeCache` class in `disk_node_cache.hpp/.cpp` con `get()`, `put()`, `exists()`, `clear()`. Usa mmap su Linux, file mapping atomico con rename, env var `CHRONON_DISK_CACHE_DIR`.

### I7 — Unificazione hash_string

`hash_string()` definito in `render_graph_hashing.hpp` come `inline`, usato da tutti i consumer. Nessuna copia duplicata.

### I8 — Riduzione boilerplate RenderCounters

`CHRONON_RENDER_COUNTERS(X)` X-macro in `counters.hpp`, genera dichiarazioni atomic, reset, copy/move constructor da un'unica lista.

### S1 — io_uring per Pipe FFmpeg

2026-05-29/06-05 — io_uring implementato in `ffmpeg_pipe_encoder.cpp` con `IORING_OP_WRITE_FIXED`, supporto registered buffers, fallback a write() classico.

### S4 — OpenEXR DWAA Bake

2026-05-22/23 — OpenEXR in vcpkg.json, `image_writer.cpp` con tiled writes 256×256 e DWAA compression, `command_bake_layer` con flag `--exr-bake`.

### S5 — PathFlattenCache con LruCache

2026-05-25 (03ebc97) — `path_cache.hpp` con `LruCache<CacheKey, shared_ptr<const vector<PathContour>>>` (16 shard, 64 MB), usato in `path_rasterizer.cpp` via `flatten_path_cached()`.

### S8 — RenderCountersRaw + merge_tls()

`RenderCountersRaw` struct POD non-atomic in `counters.hpp`. `merge_tls()` per merge in passaggio singolo a fine frame.

### S10 — SIMD Alpha Premultiplication

`simd::premultiply_alpha_rgba8()` in `highway_kernels.cpp` (HWY_DYNAMIC_DISPATCH), usato in `ImageCache::get_or_load_shared()`.

### M6 — ImageCache LRU con Memory Budget

`ImageCache` ora usa `cache::LruCache<std::string, std::shared_ptr<CachedImage>>` con memory budget via `CHRONON_IMAGE_CACHE_MAX_MB`.

### M8 — .clang-tidy + CI Static Analysis

2026-05-25/29 (a469593, d6043b1) — Config con 18 categorie di checks + job CI.

### M9 — CancellationToken + SIGINT Handler

2026-05-25 (4ce4dfb) — `cancellation_token.hpp/.cpp` con `is_cancelled()`, `cancel()`, `reset()` + handler SIGINT/SIGTERM via `sigaction`.

### M10 — CLI --dry-run

2026-05-25/06-10 — Flag `--dry-run` per validazione pre-render in `command_video.cpp`.

### N11 — Fix Double Registration Path

2026-05-25 (03ebc97) — Rimossa registrazione duplicata `create_shape_processor()` per `ShapeType::Path` in `builtin_processors.cpp`.

### N12 — Path Flatten Cache

Unificato con S5.

### N14 — Hot-Path Benchmarks

3 benchmark in `tests/bench/micro_benchmarks.cpp`: blur performance, compositing normal blend, motion blur accumulation.

### L6 — HarfBuzz Text Shaping

2026-06-02/09 (8eb9d85, 80f0299) — `FontEngine` class con FreeType + HarfBuzz integration. `shape_text()` produce `GlyphRun` con posizioni precise, cluster mapping, glyph bbox cache con LRU eviction.

---

## Refactoring

### R1 — Umbrella header invertito (opt-in)

2026-06-11 — `include/chronon3d/chronon3d.hpp`:
- Logica invertita: `#ifndef CHRONON3D_LEAN_UMBRELLA` (opt-out) → `#ifdef CHRONON3D_LEGACY_FULL_UMBRELLA` (opt-in)
- `chronon3d.hpp` ora è **leggero di default**: include solo l'API pubblica scene-building
- Per il vecchio comportamento (runtime + internal): compilare con `-DCHRONON3D_LEGACY_FULL_UMBRELLA`

### R2 — Split di chronon3d_c_api.cpp in 4 file

2026-06-11 — `src/c_api/chronon3d_c_api.cpp` suddiviso in:
- `src/c_api/c_api_context.cpp` — contesto + error handling + funzioni ciclo vita
- `src/c_api/c_api_compile.cpp` — parsing JSON→TOML + compilazione specscene + registry fallback
- `src/c_api/c_api_render.cpp` — rendering e salvataggio PNG
- `src/c_api/chronon3d_c_api.cpp` — ridotto alle sole 2 funzioni pubbliche extern "C"
- `src/c_api/c_api_internal.hpp` — nuovo header interno condiviso (`chronon_context` struct + dichiarazioni helper)
- `CMakeLists.txt` aggiornato con i 4 file sorgente

### R3 — Renderer State Refactoring

2026-06-13 — `SoftwareRenderer` state decomposed into dedicated aggregates:
- `RendererFrameHistory` — per-frame camera + fingerprint history
- `RendererDirtyTelemetry` — dirty-rect / tile-execution telemetry counters
- `RendererLayerHistory` — per-layer bbox state for frame-to-frame diffing
- `LayerBBoxState` — per-layer bounding box + diff state
- `RendererBufferRing` — managed ping-pong framebuffer ring (replaces raw `m_ping_fb[]`)
- `TransformScratchBuffer` — managed transform scratch buffer (replaces raw `m_transform_scratch`)
- `CompiledGraphCache` — managed cached compiled render graph (replaces `m_cached_compiled_graph`)

**New headers:**
- `include/chronon3d/backends/software/renderer_types.hpp`
- `include/chronon3d/backends/software/buffer_ring.hpp`
- `include/chronon3d/backends/software/scratch_buffer.hpp`
- `include/chronon3d/backends/software/graph_cache.hpp`

**Deleted headers:**
- `include/chronon3d/backends/software/software_renderer_internal.hpp` — removed; migrate includes to the four new headers above.

**API changes:**
- `TextAnchor` is now an `enum class : u8` (was a struct). Remove `.align` / `.padding` accessors — use `TextStyle::align` and `TextStyle::vertical_align` directly.
- `project_layer_2_5d()` Mat4 overload gains `bool diagnostics_enabled = false` default parameter.
- `SceneBuilder` and `LayerBuilder` are no longer transitively included — add `#include <chronon3d/scene/builders/scene_builder.hpp>` explicitly.

---

## Altri completamenti

- **SIMD Rect Rasterizer** — `rasterize_rect_simd()` via Highway in `highway_kernels.cpp`
- **Framebuffer Pipeline Diagnostics** (2026-05-24, f87c2c7) — 7 counter in `counters.hpp` con pipeline C++ → DB → React frontend
- **React Telemetry Dashboard — FB Pipeline Cards** — 6 metric cards nella sezione "Framebuffer Pipeline Diagnostics"
- **ffmpeg pipe writer cleanup** — queue con flag atomici, error handling uniforme
- **Root directory cleanup** — documenti spostati in `docs/`, script in `tools/`
- **LICENSE + CONTRIBUTING.md** — documenti per contributori

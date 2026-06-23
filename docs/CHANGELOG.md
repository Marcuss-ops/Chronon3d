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

2026-05-29/06-05 — io_uring implementato in `ffmpeg_pipe_encoder.cpp` con `IORING_OP_WRITE_FIXED`, supporto registered buffers, fallback a write() classico. *(File migrato in `commands/video/exporters/` a giugno 2026.)*

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

### L7 — PlacedGlyphRun + Unificazione Shaping

2026-06-10/14 — `PlacedGlyphRun` e `resolve_placed_glyph_run()` come unica source of truth per posizionamento glifi. Tracking-aware advance, cluster info, byte-range mapping. Eliminata logica duplicata tra `HbToBlGlyphRun`, `FtGlyphPathBuilder`, typewriter, e TextAnimator. `HbToBlGlyphRun::from()` converte HarfBuzz→Blend2D con scale in font design units.

### L8 — Bidi Segmentation + RTL Support

2026-06-10/14 — `bidi_segmenter.cpp` con FriBidi per segmentazione bidirezionale. `TextDirection::Auto/RTL/LTR` con auto-detection HarfBuzz. `TextShaping` struct con script, language, direction. Supporto Arabic (NotoNaskhArabic), Hebrew, Devanagari, CJK via `hb_buffer_guess_segment_properties()`. Font assets Arabic inclusi.

### L9 — Text Material: Bevel Sliding-Window + Ink Sampling

2026-06-10/14 — Bevel in `text_material.cpp` riscritto con sliding-window maximum separabile (deque): O(w×h) invece di O(w×h×bp). Ink trimming in `text_rasterizer_render.cpp` usa scansione campionata stride 4×2 (8× meno pixel). Descender margin per proteggere g/p/q/y/j.

### L10 — Text Gradient Fill + Stroke Gradient

2026-06-10/14 — `apply_text_fill_style()` e `apply_text_stroke_style()` supportano gradienti lineari, radiali e conici via `FillStyle`. Stroke usa `FtGlyphPathBuilder` per outline FreeType shaped da HarfBuzz, garantendo GSUB consistente per Arabic/Devanagari.

### L11 — GlyphAtlas per-Glyph Cache + Hot-Path Integration

2026-06-10/15 — `GlyphAtlas` con LRU cache per-glyph (32MB default, 8 shard, `shared_mutex`). Keyed by `(font_path, glyph_id, font_size)` con `fill_color_rgba` per matching colore. `glyph_atlas_store_from_placed_run()` estrae bitmap individuali da testo HarfBuzz-shaped. **Integrato nel percorso critico** di `text_rasterizer_render.cpp`: solid-color fill → lookup per-glyph → hit blita da atlas (salta `fillGlyphRun`) → miss renderizza + store post-`ctx.end()`. 3 counter profiling: `glyph_atlas_hits/misses/stored`.

### L12 — FontEngine Shared Mutex + Two-Phase Locking

2026-06-10/14 — `FontEngine::Impl` usa `std::shared_mutex` con `shared_lock` per letture e `unique_lock` per inserimento. `can_load()` usa pattern a due fasi (shared→exclusive su miss). Glyph bbox cache con LruCache sharded (2 shard).

### L13 — ConicGradient + Graphics Namespace

2026-06-10/14 — `GradientDefinition` nel namespace `graphics/` con stable-sort stops, binary-search opacity, spread types (pad/reflect/repeat). ConicGradient support via Blend2D `BLConicGradientValues`. Integrato in `FillStyle`/`StrokeStyle` con `KeyframeTrack`.

### L14 — Depth-Aware Scanline Rasterizer (P4)

2026-06-13/14 — Rasterizzatore scanline depth-aware con depth test per-pixel e 3D raster scanning. Card3D depth test integrato. `LensModel` unificato con GateFit e presets lens-aware.

### L15 — Golden Visual Test Suite + Gradient Tests

2026-06-10/14 — Suite golden visual con 15 reference images. Test matematici gradient da 9 a 23 casi. Test determinismo per rendering gradient. Integrazione in CMake.

### L16 — SIMD AVX2 PRGB32↔Color Conversion + Unit Tests

2026-06-15 (`9af5e9ff`) — Implementazione full AVX2 2-pixel path per `bl_image_prgb32_to_color_row` e `color_to_prgb32_row` in `highway_color_kernels.cpp`. Pattern: ScalableTag 8 lanes, LowerHalf/UpperHalf per-pixel math, transparent-pair fast-path, prefetch, scalar remainder fallback. Chiamanti aggiornati in `text_glow.cpp` (rimosso `const_cast`). Fix pre-esistenti CMakeLists.txt (extra `endif`, `find_package` duplicato, concatenazione `)if(`). 28 unit test in `test_simd_kernels.cpp` (opaque/transparent/50%-alpha, scalar reference, AVX2 pair paths, near-zero alpha, odd pixel count, determinismo 10k pixel, round-trip PRGB32↔Color ±1, batch sizes 1..17).

### L17 — PRGB32↔Color SIMD Benchmark

2026-06-15 (`64f2cd8e`) — Benchmark standalone (std::chrono, no Google Benchmark dependency) per PRGB32↔Color SIMD vs scalar a 4 larghezze (256, 640, 1920, 3840px) con ~60% transparent padding (glow pattern). **Risultato**: SIMD AVX2 2-pixel è 1.5-2.7× più lento dello scalare per righe tipiche. Il bottleneck è lo scalar unpack/pack dei uint32 (bit shifts, `1/a` division), non il calcolo float SIMD. Solo 1920px PRGB32→Color mostra 1.12× speedup (transparent-pair fast-path). **Conclusione**: serve integer SIMD unpack (`ScalableTag<uint32_t>` + `ShiftRight<24>` + `And` + `ConvertTo<float>`) per speedup reale.

---

## Refactoring

### R1 — Umbrella header invertito (opt-in)

2026-06-11 — `include/chronon3d/chronon3d.hpp`:
- Logica invertita: `#ifndef CHRONON3D_LEAN_UMBRELLA` (opt-out) → `#ifdef CHRONON3D_LEGACY_FULL_UMBRELLA` (opt-in)
- `chronon3d.hpp` ora è **leggero di default**: include solo l'API pubblica scene-building
- Per il vecchio comportamento (runtime + internal): compilare con `-DCHRONON3D_LEGACY_FULL_UMBRELLA`
- *(Rimosso a giugno 2026 — l'header è ora definitivamente leggero.)*

### R2 — Split di chronon3d_c_api.cpp in 4 file

2026-06-11 — `src/c_api/chronon3d_c_api.cpp` suddiviso in:
- `src/c_api/c_api_context.cpp` — contesto + error handling + funzioni ciclo vita
- `src/c_api/c_api_compile.cpp` — parsing JSON→TOML + compilazione specscene + registry fallback
- `src/c_api/c_api_render.cpp` — rendering e salvataggio PNG
- `src/c_api/chronon3d_c_api.cpp` — ridotto alle sole 2 funzioni pubbliche extern "C"
- `src/c_api/c_api_internal.hpp` — nuovo header interno condiviso (`chronon_context` struct + dichiarazioni helper)
- `CMakeLists.txt` aggiornato con i 4 file sorgente

### R6 — PR-2 rewire close-out: RenderGraph& execute() overloads + ExecutionPlanCache RETIRED

2026-06-21 — The PR-2 rewire followups that were documented in
`docs/refactor-roadmap/02-compiled-graph-only.md` (PR 2.3 + PR 2.4)
landed.  Three classes of public surface were RETIRED in this
delivery; the frontier is now `RenderGraph → FrameGraphCompiler →
CompiledFrameGraph → GraphExecutor::execute`.

**RenderGraph& execute() overloads dropped** — the two
`GraphExecutor::execute(RenderGraph&, ...)` overloads (4-arg and
5-arg variants with explicit `GraphNodeId output`) were removed.
Production callers, tests, and the bake CLI all now route through
`FrameGraphCompiler::compile()` first, then call the surviving 4-arg
`execute(CompiledFrameGraph&, RenderGraphContext&, RenderSession&,
ExecutionScheduler&) const` overload.  The two 6-arg overloads that
took `CompileFrameGraph&, ctx, session, scheduler, plan_cache=...`
are also gone.

**The `ExecutionPlanCache* plan_cache` parameter retired** — the
`execute(CompiledFrameGraph&, ...)` overload no longer takes a
topological-plan cache.  The legacy `(void)plan_cache;` keep-alive
and the 3-step cache lookup logic in the raw-graph implementation
are both gone.  Compiled graphs already carry their plan on
`CompiledFrameGraph::levels`, so the cache layer was redundant.

**The `chronon3d::runtime::ExecutionPlanCache` class itself
deleted** — with no remaining consumers in production, debug, test,
or bench paths, the class + header + source were removed.  Plumbing
on `RenderRuntime` (`m_owned_plan_cache`, `plan_cache()` accessor,
`RenderServices::plan_cache` field) and on `SoftwareRenderer`
(`plan_cache()` accessor) was dropped.  `SessionServices::plan_cache`
field removed.  The `execution_plan_cache_hits` counter macro
removed from `CHRONON_COUNTERS_GRAPH` (auto-propagates to the
counters struct, `kCounterNames`, and the merged-into counter
registry).

**6 caller migrations** — the bake CLI (`command_bake_layer`),
the debug pipeline (`debug.cpp`), the RenderBackend test cases
(`test_render_backend.cpp`), and the always-CompiledFrameGraph
callers (`scene_tile_execution`, `tile_execution_coordinator`,
`precomp_node_execute`) all migrated to the compiled-graph
contract.  The bake CLI additionally uses
`RenderGraph::retarget_output(selection.selected_output)` so the
selected layer becomes the compiled graph's output without a
rebuild.

**CI enforcement** — `tools/check_architecture_boundaries.sh` now
has a 5th grep guard (header text bumped from "4 total" to "5
total" and all four pre-existing labels unified to `[N/5]`) that
flags any reintroduction of `plan_cache` references across
`include/`, `src/`, `tests/`, and `apps/`.  `docs/` is intentionally
excluded so that the WP-2 / WP-8 audit-trail comments survive.
Exit criterion `git grep plan_cache -- include/ src/ apps/ tests/`
now returns zero hits.

**External SDK migration note (breaking change).**  Three public
symbols were RETIRED:

1. `chronon3d::runtime::ExecutionPlanCache` (class) and its header
   `<chronon3d/runtime/execution_plan_cache.hpp>` — file deleted;
   downstream code that used the type must drop the include and
   either re-derive plans via `FrameGraphCompiler::compile()` or
   call `GraphExecutor::execute(CompiledFrameGraph&, ...)` without
   supplying a plan cache.  Before:

   ```cpp
   #include <chronon3d/runtime/execution_plan_cache.hpp>
   chronon3d::runtime::ExecutionPlanCache plan_cache;
   executor.execute(graph, output, ctx, session, scheduler, &plan_cache);
   ```

   After (render-the-whole-graph shape):

   ```cpp
   #include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
   auto compiled = FrameGraphCompiler{}.compile(std::move(graph), ctx);
   executor.execute(compiled, ctx, session, scheduler);
   ```

   After (bake-style layer retarget shape — e.g. the bake CLI):

   ```cpp
   #include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
   // DELTA: retarget the output node before moving the graph.
   // retarget_output works in either phase (Building or Frozen) so
   // you can layer-select without rebuilding the whole graph.
   graph.retarget_output(selection.selected_output);
   auto compiled = FrameGraphCompiler{}.compile(std::move(graph), ctx);
   executor.execute(compiled, ctx, session, scheduler);
   ```

   Note: `FrameGraphCompiler::compile` takes `RenderGraph` **by
   value** — the `std::move(graph)` call consumes it.  If you need
   information from the graph after `compile()`, capture the
   `CompiledFrameGraph` fields your downstream code reads instead
   of trying to retain the raw graph: `compiled.structure_hash`,
   `compiled.levels`, per-node `compiled.nodes[id]` records
   (including `stable_node_id` and `cache_policy`), and
   `compiled.graph_instance_id`.  `RenderGraph` has no working
   copy API — its copy ctor is implicitly deleted because the
   move ctor is user-declared — so callers that genuinely need the
   raw graph post-execute must re-derive it from the scene via
   `GraphBuilder::build(scene, ctx)`.

2. `chronon3d::SoftwareRenderer::plan_cache()` accessor — gone;
   there is NO replacement because topological plans now live
   exclusively on `CompiledFrameGraph::levels`.  Before:

   ```cpp
   executor->execute(compiled, ctx, session, scheduler,
                     sw_renderer->plan_cache());
   ```

   After:

   ```cpp
   executor->execute(compiled, ctx, session, scheduler);
   ```

3. `chronon3d::runtime::SessionServices::plan_cache` field (and
   the matching literal in `make_session()`) — gone; callers that
   previously wrote a `plan_cache` line in their
   `SessionServices{…}` initializer must drop it.  Before:

   ```cpp
   chronon3d::SoftwareRenderSession session;
   session.services = SessionServices{
       .executor            = runtime.services().executor,
       .plan_cache          = runtime.services().plan_cache,
       // ...
   };
   ```

   After:

   ```cpp
   chronon3d::SoftwareRenderSession session;
   session.services = SessionServices{
       .executor            = runtime.services().executor,
       // plan_cache line dropped
       // ...
   };
   ```

No semantic replacement API exists — route through
`FrameGraphCompiler` and let the compiled graph be the plan.

### R5 — WP-8 close-out: `render_session.hpp` is engine-generic again

2026-06-21 — The three structural cross-layer dependencies that WP-8
flags on `include/chronon3d/runtime/render_session.hpp` are
**resolved**:

**TICKET-013 — scene_hasher relocated**
- `graph::SceneHasher scene_hasher;` was a value member on
  `RenderSession`.  It is now `m_owned_scene_hasher{}` on
  `RenderRuntime` (default-constructible value, `populate()` does
  not have to reserve it explicitly — runtime owns).
- `RenderSession` reaches it through `services.scene_hasher`
  (new pointer in `SessionServices`), populated by
  `runtime::make_session()`.
- The header `<chronon3d/runtime/render_session.hpp>` now only
  forward-declares `graph::SceneHasher`; the include
  `<chronon3d/render_graph/core/scene_hasher.hpp>` is gone.

**TICKET-014 — software_session_resources include dropped**
- The legacy use of `SoftwareSessionResources` inside this header
  was eliminated when WP-3 PR 3.4 closed out the legacy
  `SoftwareRenderSession` struct from this file.  Post-WP-3 the only
  remaining reference was a doc-comment block — the include is now
  gone.

**TICKET-017 — scene_program_store relocated**
- `std::unique_ptr<graph::SceneProgramStore> program_store{...}`
  was a member on `RenderSession`.  It is now
  `m_owned_program_store` on `RenderRuntime` (unique_ptr; runtime
  constructs it in `populate()`).
- `RenderSession` reaches it through `services.program_store`
  (new pointer in `SessionServices`); `reset_job()` now calls
  `services.program_store->clear()` instead of touching the field
  directly.
- The header only forward-declares `graph::SceneProgramStore`; the
  include `<chronon3d/render_graph/cache/scene_program_store.hpp>`
  is gone.

**Migration details**
- New file: `src/runtime/render_session.cpp` holds the bodies for
  the four accessor methods (`scene_hasher()`, `program_store()`,
  non-const + const overloads) and the `reset_job()` body.  Pulled
  out of the runtime/ header so the header stays free of
  `render_graph/core/scene_hasher.hpp` and
  `render_graph/cache/scene_program_store.hpp`.
- `RenderRuntime::RenderServices` extended with two pointer
  members: `graph::SceneHasher* scene_hasher` and
  `graph::SceneProgramStore* program_store`.
- `runtime::SessionServices` extended with the same two pointer
  members; `runtime::make_session()` populates them.
- `render_session.hpp`-driven `m_session.scene_hasher()` callers
  (e.g. `SoftwareRenderer::scene_hasher()`) continue to work
  because the canonical accessor method on
  `RenderSession` now proxies through `services`.
- `SoftwareRenderer::scene_hasher()` and the new
  `SoftwareRenderer::program_store()` accessors route through
  `m_runtime->scene_hasher()` /
  `m_runtime->program_store()`.

**Boundary test** — the dict `KNOWN_VIOLATIONS` in
`tests/architecture/test_render_session_includes_boundary.py` is
empty; the test now outputs
`OK: include-graph boundary invariants satisfied (errors=0).`
without emitting any `INFO:` lines.

**Shared-state note (read first)** — `scene_hasher` + `program_store`
now live on `RenderRuntime` (engine-lifetime), not on `RenderSession`
(per-render-job).  Therefore `RenderSession::reset_job()` mutates
runtime-owned SHARED state via the SessionServices back-pointers.
In a single-engine-instance deployment (the canonical production
shape — one runtime, one renderer, one session) this matches the
previous per-session isolation semantics, because there is only one
instance of each field.  In any deployment that constructs multiple
`SoftwareRenderers` from a shared `RenderRuntime`, or multiple
`SoftwareRenderSession`s from one Runtime, scene_hasher +
program_store state will be SHARED across those instances —
`reset_job()` will reach across them.  If your code relied on
per-session isolation for these caches (e.g. concurrent render
jobs with separate fingerprints), this WP-8 close-out is a
behavior change for that pattern.  Workaround if per-job
isolation is required: construct a separate `RenderRuntime` per
job.

### R4 — WP-3 PR 3.4 close-out: legacy full-reset shim RETIRED + legacy `SoftwareRenderSession` removed

2026-06-21 — The WP-3 PR 3.4 close-out completed the reset-semantics
consolidation that PR 3.4 introduced.  Three changes landed across
two commits (dc9f1cfa + the follow-up ODR consolidation):

**Reset-API retirement** — The legacy full-reset shim on
`RenderSession` (and the corresponding wrapper on the historical
duplicate `SoftwareRenderSession`) was removed entirely.  Callers
migrated to the explicit reset APIs: `reset_frame_temporaries()`
(frame-scoped telemetry reset) or `reset_job()` (full session reset).
The only live caller, `SoftwareRenderer::clear_caches()`, was
migrated in the same delivery.

**Struct ODR consolidation** — `SoftwareRenderSession` was hoisted
out of the legacy duplicate location at
`<chronon3d/runtime/render_session.hpp>` and now lives exclusively at
its canonical location
`<chronon3d/backends/software/software_render_session.hpp>`.  This
eliminates an ODR risk (two struct definitions with identical members
across two headers).  `software_renderer.hpp` was updated to include
the canonical header directly.

**New CI enforcement** — `tools/check_architecture_boundaries.sh`
now has a 4th grep check (the renumbering was `[N/3]`→`[N/4]`) that
flags any reintroduction of the retired reset shim across
`include/`, `src/`, `tests/`, and `apps/`.

**External SDK migration note** — if your code previously did

```cpp
#include <chronon3d/runtime/render_session.hpp>
// uses chronon3d::SoftwareRenderSession
```

you must now also include the canonical struct:

```cpp
#include <chronon3d/runtime/render_session.hpp>
#include <chronon3d/backends/software/software_render_session.hpp>
// uses chronon3d::SoftwareRenderSession
```

Downstream consumers using only `RenderSession` (not the
`SoftwareRenderSession` wrapper) need no changes.

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

---

## P1 — High-value cuts dopo baseline

### PR-pre-7a — Unblock cmake configure per `chronon3d_text_preset_visual_tests`

2026-06-23 — Hotfix pre-7a (unblocker per PR-7a). Il file
`tests/text_preset_visual_tests.cmake` (introdotto da PR-A4) aveva tre
problemi che impedivano `cmake --preset linux-ci` di configurare su
`main` anche dopo il taglio Taskflow:

1. **Manca `${TEST_MAIN}`** — l'`add_executable(chronon3d_text_preset_visual_tests ...)`
   non includeva `tests/test_main.cpp` (che `#define DOCTEST_CONFIG_IMPLEMENT`
   fornisce `main()`) → link step falliva con `undefined reference to 'main'`.
2. **Alias doctest inesistente** — `target_link_libraries(...)` referenziava
   `doctest::doctest_with_main`, target alias non esportato dal port vcpkg
   di doctest (esporta solo `doctest::doctest`).
3. **Runtime asset resolution** — aggiunto
   `WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}` all'`add_test(...)` per
   specchiare il pattern canonico di `tests/visual_tests.cmake`, in modo
   che `tests/test_main.cpp`'s `test_assets.mount(current_path())` monti
   rispetto alla root del repository e `assets/fonts/Poppins-Bold.ttf`
   (citato nel cpp del test) risolva correttamente.

**Sweep**: `grep -rn 'doctest::doctest_with_main\|DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN' tests/` → **0 hit** fuori da questo file (anti-pattern unico, root cause isolato).

**Machine verification**: `cmake --preset linux-ci` sul branch
`codex/pre-7a-fix-text-preset-visual-cmake` raggiunge
`Configuring done (2.1s) / Generating done (0.8s)` con `rc=0`.

Branch: `codex/pre-7a-fix-text-preset-visual-cmake`.

## Expression System v2 — Lifecycle (PR #23 → guard retirement)

Provenance trail for `expressions/v2` through the repo, 2026-06-20.

| Event | Where | Status |
|---|---|---|
| `expressions/v2` engine landed independently on `main` via PR #23 (disabled test fixtures under `tests/expressions/`) | repo history | ✅ Merged |
| Surviving PR #23 defects filed | `docs/FOLLOWUP_TICKETS.md` → TICKET-003 (lexer.hpp `<chrono3d/...>` typo) + TICKET-004 (`PUBLIC ${CMAKE_SOURCE_DIR}` include bug on `chronon3d_expressions_v2`) | 🟢 Resolved (both DONE) |
| CMake guard `CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2` rolled back (option kept as deprecated no-op for cache-key compatibility) | `CMakeLists.txt:200-237` | ✅ Retired |
| Stale flag references in CI, presets, scripts, configs | audited broadly (`.cmake`, `CMakeLists.txt`, `.sh`, `.yml`, `.yaml`, `.json`, `.py`, `.toml`, `.cfg`, `.ini`, `.env`, `Dockerfile*`, `.github/workflows`, `vcpkg.json`, `tools/*.sh`, repo-wide MD) | none — clean |
| Doc sweep recording `expressions/v2` is now on `main` | this file + `FEATURES.md` + `ROADMAP.md` + `ARCHITECTURE_EVOLUTION_PLAN.md` | ✅ Recorded |

> Status (2026-06-21): `expressions/v2` lives in the **Experimental Zone** (see
> `ARCHITECTURE_EVOLUTION_PLAN.md`). The engine was merged on `main` via PR
> #23. TICKET-003 and TICKET-004 (the surviving PR #23 defects) are
> **🟢 Done**. The next blocker for stable-feature promotion is
> **TICKET-EXP2-G3** (Gate 3 Path A → Path B migration in
> `docs/FOLLOWUP_TICKETS.md`).

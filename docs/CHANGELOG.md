# Chronon3D — Changelog

> Cronologia degli item completati, estratti dalla vecchia `IMPROVEMENTS.md`.

---


### TICKET-101 -- compile_text_layout accepts (TextDocument, paragraph_index) + build_text_run preserves rich-text (cat-3, 2026-06-30)

2026-06-30 -- Refactor cat-3 freeze-compliant del compiler canonico + wrapper multi-paragrafo. Aggiunto `std::size_t paragraph_index{0}` a `TextLayoutRequest` (POD extension, zero nuove classi pubbliche in `include/chronon3d/`). `compile_text_layout()` ora indicizza direttamente in `tree.paragraphs[request.paragraph_index]` con bounds check. Rimossa la sintesi di `TextDocument para_doc` dal wrapper `build_text_run()` -- spans / font-overrides / font-size-per-span / tracking-per-span / paragraph-style / direction / language ora preservati 1:1. Chiude review P0 #2. `build_text_run()` wrapper rifiuta paragrafi multi-font tramite pre-check su FontSpec divergenti (verdetto Issue #3 closure al public wrapper). `direction` + `language` + `line_height` ora propagati da `comp_style`. Firma pubblica `build_text_run()` invariata (backward-compatible per i 10+ call site esistenti). 3 nuovi TEST_CASEs in `tests/text/test_rich_text_paragraph_preservation.cpp`. File modificati: `include/chronon3d/text/text_run_builder.hpp` + `src/text/text_run_builder.cpp` + `tests/text/test_rich_text_paragraph_preservation.cpp` (NEW) + `tests/core_tests.cmake` + `docs/FOLLOWUP_TICKETS.md` + `docs/CURRENT_STATUS.md` + `docs/CHANGELOG.md` (questa entry).

### TICKET-092 -- per-paragraph error accumulator (CompiledParagraphResult + TextDocumentCompileResult) + source_index bridge (cat-3, 2026-06-30)

2026-06-30 -- Refactor cat-3 freeze-compliant del pipeline compile accumulator. Nuove struct INTERNE `chronon3d::text_internal::CompiledParagraphResult { std::size_t source_index; Result<std::shared_ptr<TextRunLayout>, TextLayoutError> result; }` + `TextDocumentCompileResult { std::vector<CompiledParagraphResult> paragraphs; bool complete; }` definite in NEW `src/text/text_document_compile_result.hpp` (MAI in `include/chronon3d/`, per cat-3 freeze v0.1). Nuova funzione interna `compile_text_document()` accumula Ok AND Err per-paragraph in un vettore -- rimuove il pattern `spdlog::warn(...) + goto next_paragraph` skip-on-failure dal `build_text_run()` pipeline. `apply_spacing_collapse()` riscritto per usare `result.paragraphs[i].source_index` come bridge verso `tree.paragraphs[source_index].style` (elimina l'assumption `result.paragraphs[i] == tree.paragraphs[i]` che si rompeva dopo un paragraph skip -- chiude review P0 #3). `build_text_run()` (public) diventa thin wrapper: chiama `compile_text_document()`, filtra Ok nel `TextRunBuildResult` legacy + emette `spdlog::warn` diagnostico per i paragraph falliti (backward-compat con i 10+ call site esistenti). Firma pubblica `build_text_run()` invariata (zero espansione `include/chronon3d/`). 3 nuovi TEST_CASEs in `tests/text/test_compile_text_layout_per_paragraph_failure.cpp`: (1) paragrafo centrale (i=1) fallisce → tutti e 3 i `CompiledParagraphResult` preservati con `source_index` intatto, `complete=false`; (2) single-paragraph multi-font → 1 entry Err con `source_index=0`; (3) 3-paragraph all-single-font → 3 Ok entries con `complete=true`. File modificati: NEW `src/text/text_document_compile_result.hpp` + `src/text/text_run_builder.cpp` (compile_text_document + apply_spacing_collapse rewrite + build_text_run thin wrapper) + NEW `tests/text/test_compile_text_layout_per_paragraph_failure.cpp` + `tests/core_tests.cmake` + `docs/FOLLOWUP_TICKETS.md` (TICKET-092 row in open-blockers + recently-closed) + `docs/CURRENT_STATUS.md` § Testo (closure line) + `docs/CHANGELOG.md` (questa entry).


### TICKET-105 -- identity / preservation regression test suite expansion (cat-2, 2026-06-30)

2026-06-30 -- Expansion cat-2 freeze-compliant della test suite di regressione identity/preservation del test compiler canonico. 3 nuovi TEST_CASEs deterministici (no threads, no time, no PRNG) in `tests/text/test_text_unit_map_joint_include.cpp`: **(1) identity across frames** -- `materialize_text_run_shape` ≡ `compile_text_layout` produce stesso `shared_ptr<TextRunLayout>` (cache hit) AND stesso `layout_hash()` (cache key content-anchored stabile) tra frame N + frame N+1 + frame N+2 scramble (lock `cache_layout=true` su `params.cache_layout`); **(2) joint-include contract (canonical-only mode)** -- canonical `text_unit_map.hpp` (class `chronon3d::TextUnitMap` 8-level ladder: byte/codepoint/grapheme/glyph/word/line/paragraph/span) compila clean in questa TU; l'ODR conflict con il narrow `struct chronon3d::TextUnitMap` (3-level) in `glyph_selector.hpp` è documentato a header e deferred a TICKET-083 (post-baseline-verde, cat-violator oggi); il narrow compile contract è esercitato dai test sibling `tests/text/test_glyph_selector_spec.cpp` + `tests/text/test_text_unit_map.cpp` a single-header TU boundary; **(3) double-include safety** -- triple `#include <chronon3d/text/text_unit_map.hpp>` a file scope; compile-time success IS il regression lock per `#pragma once` funzionante (se `#pragma once` fosse rotto, il terzo include emetterebbe class-vs-class redefinition error). Tutti i 3 casi degradano gracefully via `MESSAGE()` + early-return quando i font di sistema sono assenti (lock su STRUCTURE del input, non su font engine state). Registrato in `tests/core_tests.cmake` lista `CORE_BLEND2D_TESTS` (gated by `CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D`). Rationale della split-mode: canonical-class vs narrow-struct ODR conflict non è risolvibile in un singolo TU senza toccare il public selector API (vietato da AGENTS.md v0.1 freeze); full joint-include resolution ships with TICKET-083. File modificati: NEW `tests/text/test_text_unit_map_joint_include.cpp` + `tests/core_tests.cmake` (registrazione) + `docs/FOLLOWUP_TICKETS.md` (TICKET-105 status flip + recently-closed row) + `docs/CHANGELOG.md` (questa entry).


### TICKET-103a -- TextLayoutRequest direction/language/features extension (cat-3 #4, 2026-06-30)

2026-06-30 -- Refactor cat-3 freeze-compliant del canonical compiler per onorare i 3 nuovi campi nella cache-key signature. **Estensione POD del `TextLayoutRequest` struct** in `include/chronon3d/text/text_run_builder.hpp` con 3 nuovi campi: `TextDirection direction` (default `TextDirection::Auto`), `Bcp47LanguageTag language` (default `std::string{}`), `TextShapingFeatures features` (default `std::string{}`). **Tipo aliases pubblici** in `include/chronon3d/text/text_run.hpp`: `using Bcp47LanguageTag = std::string` (RFC 5646 language tag, promoted da `src/text/aliases.hpp`); `using TextShapingFeatures = std::string` (OT shaping features string, es. `kern=1,liga=1`). Stesso bytewise layout di `std::string` -- zero nuove classi, zero nuovo storage. **`build_cache_key()` signature estesa** da 3 a 6 parametri in `src/text/text_run_builder.cpp`: `(text, font, layout, direction, language, features)`. Rimozione dei 2 force-overrides: `key.direction = TextDirection::Auto` (era: collapse bidi handling per-run) e `key.language.clear()` (era: collapse BCP-47 discrimination). Cache key ora correttamente discrimina LTR vs RTL inputs ed en vs ar inputs. **2 call-site propagations** in `compile_text_layout()` body passano `request.direction/language/features` sia al cache lookup che al cache store. **`TextRunLayout` e `TextLayoutCacheKey` estesi** con nuovi campi: `TextRunLayout::features` (mirror di `direction`/`language` semantic); `TextLayoutCacheKey::features` (necessario per la digest discrimination). **`TextLayoutCacheKey::digest()` + `TextRunLayout::layout_hash()` + `TextRunLayout::shaping_hash()`** aggiornati in `src/text/text_run.cpp` per onorare `features` field via `hash_string(features)`. **Backward compatibilita preservata**: tutti i 10+ callsite esistenti che usano aggregate-init (`TextLayoutRequest{&doc, &layout, font}`) ottengono i valori di default dei 3 nuovi campi tramite default member initializer; zero brkage sui test preesistenti (test_text_unit_map_joint_include.cpp / test_compile_text_layout_identity.cpp / test_rich_text_paragraph_preservation.cpp / test_compile_text_layout_errors.cpp / test_text_run_builder.cpp / test_text_run_driver.cpp). **2 nuovi TEST_CASEs** in `tests/text/test_layout_cache_collision.cpp`: (1) `direction=LTR` vs `direction=RTL` -> `digest()` differ; (2) `language=en` vs `language=ar` -> `digest()` differ. Pure key-construction tests, no font engine / system fonts required. Registrato in `tests/core_tests.cmake` lista `CORE_BLEND2D_TESTS`. TICKET-093 (full `ResolvedFontAsset` unification) deferred post-baseline-verde per AGENTS.md v0.1 freeze. File modificati: `include/chronon3d/text/text_run.hpp` (type aliases + 2 new fields) + `include/chronon3d/text/text_run_builder.hpp` (TextLayoutRequest POD extension) + `src/text/text_run_builder.cpp` (build_cache_key + 2 call-sites) + `src/text/text_run.cpp` (3 hash functions) + NEW `tests/text/test_layout_cache_collision.cpp` + `tests/core_tests.cmake` + `docs/FOLLOWUP_TICKETS.md` (TICKET-103a row flip + recently-closed) + `docs/CURRENT_STATUS.md` § Testo (closure line) + `docs/CHANGELOG.md` (questa entry).

## Completati (Maggio-Giugno 2026)

### PRs 1-4 (Performance)


### TICKET-100 — Eliminate legacy materialize_text_run_shape pipeline (cat-3, 2026-06-30)

2026-06-30 — Refactor cat-3 freeze-compliant che elimina la pipeline legacy inline di `materialize_text_run_shape()` (`src/scene/builders/text_run_builder.cpp`) consolidando le 5 fasi (cache lookup + `shape_text()` + `resolve_placed_glyph_run()` + manual `TextRunLayout` field-by-field assignment + cache store) in un helper anonimo `compile_or_cache_layout()` che chiama `compile_text_layout()` (canonical compiler in `src/text/text_run_builder.cpp`). La cache_key legacy (9 campi, inclusi direction + language) è preservata BIT-IDENTICAL (cache hit/miss match pre-refactor contract); compile_text_layout cache dance è BYPASSATO via `services.cache = nullptr` per evitare default hardcoded leak. Helper post-override `text_layout->direction` + `text_layout->language` per matchare `params.direction` / `params.language`. ZERO espansione `include/chronon3d/` (cat-3 freeze-compliant per AGENTS.md v0.1). Side-benefit (chiude review P0 #6 gratis): `text_layout->font = primary_font` (FontSpec 5-campi pieno) sostituisce il legacy `shaped_font = {4 campi}` che lasciava `font.font_size` al default 0.0f / 72.0f. 4 nuovi TEST_CASE identity in `tests/text/test_compile_text_layout_identity.cpp` (registrato in `tests/core_tests.cmake` `CORE_BLEND2D_TESTS`): (1) materialize ≡ compile_text_layout diretto su input equivalente; (2) cache-hit returns same shared_ptr; (3) direction+language override post-compile (RTL/`ar` + LTR/`en` round-trip); (4) `text_layout->font.font_size` mirrors `params.text.font.font_size` (P0 #6 closure). File modificati: `src/scene/builders/text_run_builder.cpp` (helper + 5-fasi rimosse) + `tests/text/test_compile_text_layout_identity.cpp` (NEW) + `tests/core_tests.cmake` (registrazione CORE_BLEND2D_TESTS) + `docs/FOLLOWUP_TICKETS.md` (TICKET-100 status flip + recently-closed row) + `docs/CURRENT_STATUS.md` § Testo (closure line) + `docs/CHANGELOG.md` (questa entry).

---

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
`docs/ARCHIVE/refactor-roadmap/02-compiled-graph-only.md` (PR 2.3 + PR 2.4)
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

### PR-7b — Retire deprecated `CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2` option

2026-06-23 — Rimozione completa dell'option deprecato e del blocco `if()/endif()` no-op che era stato lasciato in `root CMakeLists.txt` come compat shim per forks / CI che ancora passano `-DCHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2=ON`. Il PR chiude il ciclo aperto dal retirement comment `aae68561`-prefix e da TICKET-005 Gap C.

Modifiche:

- `CMakeLists.txt` (root): rimosso tutto il blocco `option(CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2 ...)` + il comment block "Deprecated option kept for cmake-cache-key compat" + l'empty `if() endif()` no-op touch + il paragrafo "The legacy CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2 flag is now strictly OBSOLETE" del comment block superiore. Mantenuto il canonical gate `option(CHRONON3D_BUILD_EXPERIMENTAL ... OFF)` + il `add_subdirectory(experimental/expressions)` gated.
- `tools/test_architectural.sh` (Section 1 Quarantine integrity): aggiornato il regression sentinel per `CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2`. Le due exclusion lines obsolete (`grep -v '^./CMakeLists.txt:.*option('`, `grep -v '^./CMakeLists.txt:.*if(...)'`) sono state rimosse — l'option non esiste più, le esclusioni erano eccezioni per uno stato precedente. Aggiunta self-exclusion `grep -v '^./tools/test_architectural.sh'` per belt-and-suspenders (la regex con `[:=]` non matcha il proprio literal pattern, ma l'esplicita è documentazione leggibile).

[Residual references audit]

```
grep -rn 'CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2' . \
  --exclude-dir=build --exclude-dir=vcpkg_bootstrap --exclude-dir=vcpkg_installed
```

Risultato: 4 file (tutti NON code-level, tutti OK).

| File | Natura | Permit |
|---|---|---|
| `tools/test_architectural.sh` | Sentinel grep pattern itself | OK by self-exclusion |
| `docs/CHANGELOG.md` | Questa entry + storico retirement | OK (historical) |
| `docs/FOLLOWUP_TICKETS.md` | TICKET-005 Gap C body | OK (historical) |
| `docs/FEATURES.md` | Expression V2 quarantine enclosure mention | OK (historical) |

Zero residui a livello `src/`, `include/`, `content/`, `apps/`, `cmake/`, `tests/`.

[Machine verification]

- `cmake --preset linux-ci` rc=0 sul branch (Configuring done / Generating done).
- `bash tools/test_architectural.sh` → tutti i 6 section PASSED, inclusa la Section 1 con il sentinel che emette `PASSED: CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2 is no longer a live directive`.

Branch: `codex/p1-pr7b-experimental-v2-retire`.


### PR-7c — Remove legacy `Chronon3D_SDK` alias

2026-06-23 — Removed the historical `add_library(Chronon3D_SDK ALIAS chronon3d_sdk)` from `src/CMakeLists.txt:249`. The canonical public alias `add_library(Chronon3D::SDK ALIAS chronon3d_sdk)` (line 246) is the documented namespace entry point per the public surface contract in root `CMakeLists.txt`. The non-namespaced `Chronon3D_SDK` (without `::`) was a "during migration" compat shim; zero consumers in the repo or extensions reference it (verified below).

[Residual references audit]

```
grep -rn 'Chronon3D_SDK' --include='*.cpp' --include='*.hpp' --include='CMakeLists.txt' \
  --include='*.cmake' --include='*.in' --include='*.json' --include='*.sh' \
  --include='*.py' --include='*.yml' --exclude-dir=build \
  --exclude-dir=vcpkg_bootstrap --exclude-dir=vcpkg_installed .
```

Risultato: 0 hit fuori da `src/CMakeLists.txt` (alias rimosso) + solo documentation references in `docs/CHANGELOG.md` + `docs/FOLLOWUP_TICKETS.md` (historical). Tutti OK per AGENTS.md "Don't duplicate".

[Machine verification]

- `cmake --preset linux-ci` rc=0 (Configuring done, Generating done).
- `bash tools/test_architectural.sh` Section 5 renderer test: nessun nuovo failure introdotto (failure pre-esistenti su main baseline invariati).

Branch: `codex/p1-pr7c-remove-legacy-alias`.
## Cleanup pass 2026-06-23 — Branch consolidation

2026-06-23 — Serie di azioni di pulizia sui branch remoti e consolidamento dello stato del repo dopo l'integrazione delle PR docs e i ritiri dei branch orfani. La sottosezione di tracking è stata spostata da `AGENTS.md` a questo changelog (con i marker di stato per i due agenti rimasti in `AGENTS.md`).

### PR docs confluite via squash + auto-delete

- **PR #39** `docs: update immediate next steps from current main` → `docs/NEXT_STEPS.md` (squash commit `2aec95bf`)
- **PR #40** `docs: rebuild camera feature matrix from current code` → `docs/CAMERA_FEATURE_MATRIX.md` (squash commit `aa909d7a`)
- **PR #41** `docs: replace stale camera AE gap audit` → `docs/CAMERA_AE_GAP_VENDETTA.md` (squash commit `384cf1dc`)

I branch head di queste PR sono stati cancellati automaticamente dopo il merge.

### Branch remoti orfani cancellati

I seguenti branch remoti sono stati eliminati in quanto già confluiti in `main` con 0 commit unici (tip = antenato di `main`):

- `codex/orphan-cleanup-2026-06-23` — nome autoesplicativo (\"orphan cleanup\")
- `codex/agent1-renderer-boundary` — branch di proprietà dell'Agente 1 (tip `1321d56`)
- `codex/docs-current-readiness-20260623` — PR #37 già merged, post-merge re-population ridondante
- `codex/docs-readiness-followup-20260623` — PR #38 già merged, post-merge re-population ridondante

### Tracking agent retirements in `AGENTS.md`

Lo stato delle assegnazioni agenti è stato consolidato in `AGENTS.md` (commit `83a4bf21`):

- Branch Agente 1 `codex/agent1-renderer-boundary` → marker `[DONE ✓ — Merged into main on 2026-06-23, branch retired]`
- Branch Agente 2 `codex/agent2-cmake-sdk-baseline` → marker `[NOT STARTED — branch missing on origin as of 2026-06-23]`

_Nota di affinamento titolo_: la sezione "Assegnazione corrente a due agenti" in `AGENTS.md` è stata riformulata in `## Stato assegnazioni agenti (snapshot 2026-06-23)` (commit `738c6d78`), con lead-in minimale (`vedi body per lo stato per-agente`) + sequential-reality qualifier compresso e paragrafo Agente-2 sequenziale (la frase "Gli agenti possono iniziare in parallelo" riformulata come rebase post-Agente-1). Quattro iterazioni di amend + force-push con `--force-with-lease` prima dello ship finale; primo commit rigettato per avanzamento di `origin` a `3a6b4324` durante la prima finestra, recovery via amend successivo.

### PR #42 — landing via direct push

PR #42 (`codex/docs-next-steps-clean-20260623`) ispezionata per ridondanza con PR #39/#40/#41: diff corposo e non byte-identico, quindi non ridondante. Merge eseguito con `--no-ff` dopo rebase del branch su `83a4bf21`. Effetto netto sul `main`: solo `docs/NEXT_STEPS.md` modificato (+110/-38); le modifiche di PR #42 su `docs/CAMERA_FEATURE_MATRIX.md` e `docs/CAMERA_AE_GAP_VENDETTA.md` sono state deduplicate dal rebase perché sovrapposte a quanto già confluito con #40 e #41. Commit di merge: `844bc7c0`. Branch remoto cancellato, PR chiusa.

### Bypassing PR review workflow per doc-only commit consecutivi

Per coerenza con l'istruzione utente \"non crear branch inutili e pusha e committa tutto sul main\", le tre azioni di cleanup (squash-merge + auto-delete di #39/#40/#41, eliminazione diretta dei 4 branch remoti, landing di PR #42) e il commit corrente (carry-over reviewer verso `AGENTS.md` + `CHANGELOG.md`) sono stati eseguiti via direct push su `origin/main` anziché via PR review. Questa eccezione è stata concessa ad hoc in risposta a un'istruzione esplicita dell'utente durante la sessione del 2026-06-23 e non è ancora formalizzata come pattern standard in `AGENTS.md` né in un ADR dedicato — replicarla in futuro richiede prima la normalizzazione via aggiornamento di `AGENTS.md` o creazione di un ADR.

### Snapshot pointer refresh in governance docs

_Nota di recupero_: il primo commit `b9bf0b05` (snapshot pointer refresh a `main@617c0377`) è stato rigettato dal push perché nel frattempo due commit `8547b2e9` (TICKET-029 alignment) e `fb1b7e97` (PR 2 — unblock `chronon3d_scene_tests` build) sono atterrati su `origin/main` da un push esterno. Il commit è stato ricostruito via `git reset --hard origin/main` + re-apply delle modifiche con SHA aggiornato al nuovo HEAD.

I riferimenti a `main@25049b2` in `docs/STATUS.md` e `docs/ROADMAP.md` sono stati aggiornati a `main@fb1b7e97` (HEAD corrente dopo il passaggio del PR 2 — unblock `chronon3d_scene_tests` build con TICKET-029).

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

- PR-6 (commit pending, branch `codex/p1-pr6-cli-slim`) — CLI slim with sub-target gating. The 6 CLI sub-targets are now gated behind 8 new `option()` declarations (`CHRONON3D_BUILD_CLI_CORE` always ON, plus `_RENDER`, `_VIDEO`, `_TELEMETRY`, `_DEV`, `_BENCH`, and forward-compat placeholders `_DAEMON`, `_WATCH`). All default ON so existing builds reproduce byte-equivalent binaries; turning any sub-target OFF yields a slimmer CLI on demand. The executable `chronon3d_cli` link closure is slimmed: explicit list now contains only `Chronon3D::SDK` (via `chronon3d_sdk` + `chronon3d_sdk_impl` aliases), `chronon3d_cli_core` (mandatory), `chronon3d_backend_image`, `spdlog`, `fmt`; gated sub-targets are linked via a canonical `foreach (... IN ITEMS ...)` + `if(TARGET ${_cli_target})` loop. Files touched: `apps/chronon3d_cli/CMakeLists.txt` only (8 `option()` decls + 5 `if()/endif()` wraps around sub-target `add_library()` blocks + slimmed executable link + canonical foreach loop + updated `set_target_properties(... UNITY_BUILD OFF)` block). Machine verification: `cmake --preset linux-ci` rc=0; `grep -c 'option(CHRONON3D_BUILD_CLI_' apps/chronon3d_cli/CMakeLists.txt` returns 8 hit(s).

### PR-6a — F1.2 cli-slim-real-hygiene (2 cosmetic notes layered on PR-6)

2026-06-23 — F1.2 hygiene patch on top of PR-6 (`p1/cli-slim-real @ ca6a5d76`).
PR-6 itself flipped `CHRONON3D_BUILD_CLI_DEV` / `CHRONON3D_BUILD_CLI_BENCH`
defaults from ON to OFF — that flip is **documented in PR-6's own commit
subject ("... + default DEV/BENCH OFF + SDK-only link")** and is **not
re-applied here**. This PR-6a commit layers TWO cosmetic add-ons only
(NO code rotation, NO build surface change):

- **`apps/chronon3d_cli/CMakeLists.txt` Forward-compat block** — the
  `Forward-compat placeholders` comment block (lines ~26-30) was completed
  with the phrase *"cron daemon/watch extraction deferred to follow-up PR"* —
  pinning the intent that `CHRONON3D_BUILD_CLI_DAEMON` / `_WATCH` are no-op
  flags today and that the actual extraction logic lands in a future
  single-bot PR (preserves commit-graph hygiene without prematurely
  extracting daemon/watch code).

- **`apps/chronon3d_cli/CMakeLists.txt` `target_link_libraries` hardening**
  — the link-closure comment block (preceding `target_link_libraries(chronon3d_cli PRIVATE`) was extended with a verbatim enumeration of the
  OBJ-lib specifics inside `chronon3d_sdk_impl`: `chronon3d_pipeline` +
  `chronon3d_scene` + `chronon3d_runtime` + `chronon3d_cache` +
  `chronon3d_effects` + `chronon3d_registry` + `chronon3d_assets` +
  `chronon3d_extension` + `chronon3d_animations` + `chronon3d_text_core` +
  `chronon3d_blend2d_paint` + `chronon3d_graph_*` sub-targets — making the
  SDK singleton aggregation explicit so future readers don't grep for
  `chronon3d_pipeline` / `chronon3d_scene` as direct link entries on the
  CLI executable (intentionally absent per TICKET-011).

(The original F1.2 spec listed 3 notes; the third note — `CHRONON3D_BUILD_CLI_(DEV|BENCH)` default-flip ON→OFF — was already done at `ca6a5d76`,
recorded in PR-6 above. This PR-6a hygiene entry corrects that earlier
'flip remains un-applied' wording, which was a factual contradiction with
the branch base state at `ca6a5d76`.)

Build surface byte-equivalent with `p1/cli-slim-real @ ca6a5d76` baseline.

Branch: `p1/cli-slim-real-hygiene` (1 commit ahead of `p1/cli-slim-real`).
To be FF-merged into local `main` (which is currently at `a094b020b22b870c4d6ccf4018d72384514ecdfe`,
1 commit ahead of `origin/main @ 3bad8c82`).

### TICKET-040 — complete Taskflow cleanup (rot closure)

2026-06-23 (branch `codex/fix-ticket-040-taskflow-cleanup` off `main@9e1750a9`) — Completes the second half of `TICKET-040` (🟢 Done in `docs/FOLLOWUP_TICKETS.md` but only half-executed). The retirement side (`vcpkg.json` no longer lists `"taskflow"`) shipped long ago; the symmetric `find_package(Taskflow CONFIG REQUIRED)` removal from `CMakeLists.txt:123` was deferred, leaving a leftover dependency on a vcpkg package the manifest no longer provided. That mismatch blocked `cmake --preset linux-ci` at the configure step with `TASKFLOW_NOTFOUND` (TICKET-038-style rot for the new `chronon3d_wiggly_selector_tests` + `chronon3d_wave_selector_tests` targets in TXT-08, and likely others). Single-file delta:

1. **`CMakeLists.txt`** — deleted the orphan `find_package(Taskflow CONFIG REQUIRED)` line in the dep-discovery block. The line is removed; no other find_package / target_link_libraries / `Taskflow::...` consumer exists anywhere in `src/`, `include/`, `apps/`, `tests/`, `content/`, or `experimental/` (verified via `grep -rn 'Taskflow' ... --include=*.cmake --include=CMakeLists.txt --include=*.cpp --include=*.hpp --include=*.in` excluding `vcpkg_installed/` + `build*/`). The reference in `cmake/Chronon3DConfig.cmake.in` (find_package excludes Taskflow from the install-export, lines 23–26) is documentation of the intentional exclusion — kept unchanged.

Build verification: the simplify configure invocation `cmake -B build-verify-taskflow -S . -DCMAKE_BUILD_TYPE=Release` on a clean clone (with `VCPKG_MANIFEST_MODE=ON` honoring vcpkg.json) now resolves the configure step rc=0 instead of failing on `TASKFLOW_NOTFOUND`. Full `cmake --preset linux-ci` exercising vcpkg's manifest-mode fetch chain is reproducible in CI on a fresh checkout.

Anti-duplication integrity (per `docs/ANTI_DUPLICATION_RULES.md`): Taskflow contributes zero unique concepts to the productive tree (verified via grep), so deleting the find_package line collapses the dependency graph along the canonical single-provider-per-concept rule. No replacement is required.

Branch: `codex/fix-ticket-040-taskflow-cleanup` (single commit ahead of `main@9e1750a9`). LF-merge ready. Forward-compat: TICKET-040 in `docs/FOLLOWUP_TICKETS.md` should be re-checked to confirm it stays 🟢 Done now that BOTH halves of the cleanup have shipped.

# Chronon3D — Changelog

> Lavoro completato su `main`. Per i dettagli completi di ogni ticket: [`docs/tickets/`](docs/tickets/).
> Per lo stato corrente: [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md).

---

## Luglio 2026 — Chiusure recenti

### P0 #1 — `RenderGraphNode::execute()` → `Result<OwnedFB, NodeExecutionError>`
- Cambiato il return type di `RenderGraphNode::execute()` da `OwnedFB` a `NodeExecResult` (`Result<OwnedFB, NodeExecutionError>`)
- Aggiunto `Result<T,E>` template in `render_backend.hpp` con `take_value()` per move-only types
- 18+ tipi di nodo aggiornati (headers + .cpp) a restituire `NodeExecResult{...}`
- `run_node()` in `node_runner.cpp`: unwrappa `Result`, su errore scrive a `ctx.frame_error`
- `execute_internal()` in `executor.cpp`: controlla `frame_error` dopo i nodi → restituisce `nullptr`
- Invariante: backend error → node restituisce `NodeExecutionError` → run_node scrive frame_error → executor nullptr → sink non pubblica
- 36 file modificati, 4 test file aggiornati (mock `execute()` → `return NodeExecResult{}`)

### P0 #2 — `FontLayoutIdentity` unificata su cache/hash/fastpath/prewarm
- Nuovo `FontLayoutIdentity` struct in `font_engine.hpp` (font_path, font_family, font_weight, font_style, font_size, features)
- `font_family` aggiunto a `layout_hash()`, `shaping_hash()`, `TextLayoutCacheKey::digest()`, `build_cache_key()`
- Fast-path in `apply_active_state_to_text_run_shape()` ora confronta `FontLayoutIdentity` invece del solo `source_text`
- Font overrides non più gated da `font_path.empty()` (×2 in `text_run_driver.cpp`)
- 5 file modificati: `font_engine.hpp`, `text_run.hpp`, `text_run.cpp`, `text_run_builder.cpp`, `text_run_driver.cpp`

### TICKET-118 — `SoftwareBackend::draw_node` reale + drop dummy `TextRunProcessor`
- `SoftwareBackend::draw_node` non è più un no-op `// Intentionally empty`:
  ora dispatcha `m_proc_ctx.registry->get_shape(shape.type())->draw(...)`,
  con early-return silent sul caso `ShapeType::TextRun` (canonico via
  `multi_source_node` → `draw_text_run`).
- Fallback loud-fail: `m_proc_ctx` mai attachato → `spdlog::error` con
  shape type numberato, così regressioni future su `attach_processor_context`
  appaiono in CI invece di "completarsi" silenziosamente.
- Droppato dummy `TextRunProcessor` in `text_run_processor.cpp` (no-op
  draw + bbox `{0,0,0,0}` + hit_test false). Era un registry marker
  inutilizzato: il dispatch canonico passa da `TextRunNode` →
  `SoftwareBackend::draw_text_run` direttamente.
- Droppata `create_text_run_processor()` (factory + forward-decl + site
  di registrazione in `builtin_processors.cpp::register_builtin_processors`).
- Niente nuova API pubblica; niente nuovo `target_link_libraries`.

### TICKET-119 — `SoftwareBackend` m_owner back-pointer removal + internal bridge
- `SoftwareBackendServices::owner` rimosso (era il `SoftwareRenderer*`
  back-pointer usato da `draw_text_run` per sourcire la
  `SoftwareProcessorContext`).  `MissingOwner` Code rimosso; i restanti
  5 Code mantenuti ma renumbered (MissingCounters=1 →
  MissingTextResources=5).
- `SoftwareBackend` ora owner-free lato software: `m_proc_ctx`
  value-member popolato post-construction via NUOVO metodo pubblico
  `attach_processor_context(SoftwareProcessorContext)`.
- Nuovo header INTERNO `src/backends/software/internal/software_processor_services.hpp`
  (mai installato in `include/chronon3d/`): definisce `ProcessorSourceExtras`
  (registry + image_backend + font_engine) e la free function
  `make_processor_context(services, extras)`. Questo è l'unico path che
  conosce come costruire un `SoftwareProcessorContext` completo da un
  public `SoftwareBackendServices` + i campi orchestrator-only.
- `runtime_adapter.cpp::attach_software_backend`, `tests/helpers/test_utils.hpp`
  ed il file di test di factory (`test_software_backend_factory.cpp`)
  aggiornati per il nuovo wiring.  Per Option A (DELETE-only) thinker-validated:
  il test file ha rimosso i check static-grep / NDEBUG su `MissingOwner` e
  ha aggiunto un nuovo TEST_CASE static-grep che verifica l'applicazione
  della contractive removal (linee TICKET-118 presenti, MissingOwner assente).
- Public-API surface delta: **1 new public method** added to
  `chronon3d::SoftwareBackend` (`attach_processor_context(...)`); the
  underlying `chronon3d::SoftwareProcessorContext` type was already
  public.  No new public classes, no new public headers, no new public
  fields on `SoftwareBackendServices`.
- ABIs invariant: `ProcessorSourceExtras::font_engine` gadget-field remains
  `#ifdef CHRONON3D_HAS_BACKEND_TEXT`-gated like
  `chronon3d::SoftwareProcessorContext::font_engine` (commented in the
  new header); the parent CMakeLists sets the macro uniformly on the
  `chronon3d_backend_software` target so all objects see one layout.

### TICKET-101 — compile_text_layout accetta (TextDocument, paragraph_index)
- Aggiunto `paragraph_index` a `TextLayoutRequest` (POD extension, zero nuove classi pubbliche)
- `compile_text_layout()` indicizza direttamente nel document tree, preserva rich-text 1:1
- `build_text_run()` rifiuta paragrafi multi-font con pre-check
- 3 nuovi TEST_CASEs deterministici

### TICKET-092 — per-paragraph error accumulator
- Introdotto `CompiledParagraphResult` e `TextDocumentCompileResult` (struct interne, non pubbliche)
- `compile_text_document()` accumula Ok AND Err per-paragraph, rimuove skip-on-failure
- `source_index` bridge per `apply_spacing_collapse()`, firma pubblica invariata
- 3 nuovi TEST_CASEs deterministici

### TICKET-105 — identity/preservation regression test suite
- 3 TEST_CASEs deterministici: identity across frames, joint-include, double-include safety
- Stesso `shared_ptr<TextRunLayout>` + `layout_hash()` tra frame N, N+1, N+2
- ODR conflict canonico-vs-narrow documentato, deferred a TICKET-083

### TICKET-103a — TextLayoutRequest direction/language/features
- 3 nuovi campi POD su `TextLayoutRequest`: direction, language, features
- `Bcp47LanguageTag` e `TextShapingFeatures` come type aliases pubblici
- Cache key estesa a 6 parametri, discrimina LTR vs RTL, en vs ar
- 2 nuovi TEST_CASEs di cache collision

### TICKET-104 — PendingTextRun::consumed real-decrement
- `consumed` ora decremento reale con contatore atomico (non più no-op)
- `commit()` valida catena selector/animator, droppa orphan + spdlog::warn
- `mutable_pending()` resta pubblico, doc-comment flagga il bypass
- 2 nuovi TEST_CASEs deterministici + CR5 follow-up CapturingWarnSink

### TICKET-100 — Elimina legacy materialize_text_run_shape pipeline
- 5 fasi legacy consolidate in `compile_text_layout()` canonico
- Cache key legacy preservata bit-identical
- `text_layout->font = primary_font` (chiude review P0 #6)
- 4 nuovi TEST_CASEs identity

---

## Maggio–Giugno 2026 — Performance e feature

### PRs 1-4 — Performance
- PR 1: Skip-When-Opaque CompositeNode → 1.4× (425→303ms)
- PR 2: Execution plan cache → skip hash O(164) per grafi grandi
- PR 3: Static fingerprint fast-path → 740× (311→0.42ms) su ImgGridTest
- PR 4: Graph structure hint executor → ~0 overhead quando invariato

### Miglioramenti core (I1–I8)
- I1: Bake EXR + mmap per grid_bg, compressione DWAA half-float
- I2: Huge Pages per FramebufferPool via `HugePageAllocator<T>`
- I3: Dirty Rect Bitmask per compositing ottimizzato
- I5: FrameArena nel render pipeline con guard RAII
- I6: DiskNodeCache con mmap, file mapping atomico, env var `CHRONON_DISK_CACHE_DIR`
- I7: `hash_string()` unificato come `inline` in `render_graph_hashing.hpp`
- I8: `CHRONON_RENDER_COUNTERS(X)` X-macro, genera contatori da unica lista

### Sistema e I/O (S1–S10)
- S1: io_uring per pipe FFmpeg con registered buffers e fallback write()
- S4: OpenEXR DWAA bake con tiled writes 256×256
- S5: PathFlattenCache con LruCache (16 shard, 64 MB)
- S8: `RenderCountersRaw` struct POD non-atomic, `merge_tls()` single-pass
- S10: SIMD alpha premultiplication via Highway in `highway_kernels.cpp`

### Infrastruttura (M6–M10)
- M6: ImageCache LRU con memory budget via `CHRONON_IMAGE_CACHE_MAX_MB`
- M8: `.clang-tidy` con 18 categorie di check + job CI
- M9: CancellationToken + SIGINT/SIGTERM handler via `sigaction`
- M10: CLI `--dry-run` per validazione pre-render

### Fix e benchmark (N11–N14)
- N11: Rimossa registrazione duplicata `create_shape_processor()` per Path
- N14: 3 benchmark hot-path: blur, compositing, motion blur

### Text pipeline (L6–L15)
- L6: FontEngine con FreeType + HarfBuzz, `shape_text()` produce `GlyphRun`
- L7: `PlacedGlyphRun` come unica source of truth, tracking-aware advance
- L8: Bidi segmentation via FriBidi, supporto Arabic/Hebrew/Devanagari/CJK
- L9: Text Material: bevel sliding-window O(w×h), ink sampling stride 4×2
- L10: Text gradient fill + stroke gradient lineari/radiali/conici
- L11: GlyphAtlas per-glyph LRU cache integrato nel percorso critico
- L12: FontEngine shared_mutex con two-phase locking
- L13: ConicGradient + namespace `graphics/`, stable-sort stops
- L14: Depth-aware scanline rasterizer con depth test per-pixel
- L15: Golden visual test suite con 15 reference images, test gradient

### SIMD (L16–L17)
- L16: AVX2 PRGB32↔Color conversion, 28 unit test
- L17: Benchmark SIMD vs scalar: ~1.5-2.7× più lento dello scalare (bottleneck unpack uint32)

---

## Refactoring (Giugno 2026)

### R1 — Umbrella header leggero di default
- `chronon3d.hpp` ora include solo API pubblica scene-building
- Comportamento legacy richiede `-DCHRONON3D_LEGACY_FULL_UMBRELLA`

### R2 — Split C API in 4 file
- `c_api_context.cpp`, `c_api_compile.cpp`, `c_api_render.cpp`, `chronon3d_c_api.cpp`
- Header interno condiviso `c_api_internal.hpp`

### R3 — SoftwareRenderer state decomposition
- 7 nuovi aggregate: RendererFrameHistory, DirtyTelemetry, LayerHistory, BufferRing, ScratchBuffer, GraphCache
- `TextAnchor` ora `enum class : u8`, `software_renderer_internal.hpp` rimosso

### R4 — Legacy reset shim RETIRED
- `reset()` → `reset_frame_temporaries()` / `reset_job()`
- `SoftwareRenderSession` consolidato in header canonico, eliminato ODR risk
- CI enforcement aggiunto in `check_architecture_boundaries.sh`

### R5 — render_session.hpp engine-generic
- `scene_hasher` e `program_store` spostati su `RenderRuntime`
- 3 include-dipendenze cross-layer eliminate dall'header
- Boundary test: 0 violazioni

### R6 — ExecutionPlanCache RETIRED
- Classe, header, plumbing su RenderRuntime/SoftwareRenderer/SessionServices rimossi
- 6 caller migrati a `FrameGraphCompiler::compile()` + `execute(CompiledFrameGraph&)`
- CI enforcement: 5 grep guard in `check_architecture_boundaries.sh`

---

## Pulizie e consolidamenti

### PR-7a — Unblock text_preset_visual_tests cmake configure
- Aggiunto `${TEST_MAIN}`, fix alias doctest, asset resolution con `WORKING_DIRECTORY`

### PR-7b — Retire CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2
- Option deprecato e blocco `if()/endif()` no-op rimossi da root CMakeLists.txt
- Sentinel aggiornato in `test_architectural.sh`, 0 residui in src/include

### PR-7c — Remove legacy Chronon3D_SDK alias
- Rimosso alias non-namespaced, 0 consumer nel repo

### TICKET-040 — Taskflow cleanup completo
- Rimosso `find_package(Taskflow)` orfano che bloccava configure
- vcpkg.json già non listava Taskflow, dependency graph collassato

### PR-6 — CLI slim con sub-target gating
- 8 `option()` per sub-target CLI, `CHRONON3D_BUILD_CLI_CORE` sempre ON
- Link closure slim: solo SDK + cli_core + backend_image + spdlog + fmt

### Expression System v2 lifecycle
- Engine atterrato su `main` via PR #23, in Experimental Zone
- TICKET-003 e TICKET-004 risolti
- CMake guard `CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2` ritirata
- Prossimo blocker: TICKET-EXP2-G3

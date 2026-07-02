# Chronon3D — H1-2026 Changelog (archive)

> Questo file è l'archivio H1-2026 di [`docs/CHANGELOG.md`](../CHANGELOG.md).
>
> Contiene verbatim le voci datate `## Maggio–Giugno 2026 — Performance e feature` e
> tutto ciò che le precede nel CHANGELOG canonical pre-trim (nota
> `## Fix-forward — corrupted public-header manifest` sul commit `28004f96`,
> `## Maggio–Giugno 2026 — Performance e feature`, `## Refactoring (Giugno 2026)`,
> `## Pulizie e consolidamenti` con sottosezione `### Expression System v2 lifecycle`).
>
> Creato 2026-07-02 per rientrare nel **limite raccomandato di 150 righe**
> ([`docs/DOCUMENTATION_GOVERNANCE.md`](../DOCUMENTATION_GOVERNANCE.md) §10 —
> Definition of Done documentale). CHANGELOG.md era a 340 linee; l'archivio
> contiene tutte le voci storiche H1-2026; il CHANGELOG canonical conserva
> solo `## Luglio 2026 — Chiusure recenti` (canonical active history).
>
> Le voci attive vivono in [`docs/CHANGELOG.md`](../CHANGELOG.md) sotto
> `## Luglio 2026 — Chiusure recenti`.
>
> Contratto: tutto ciò che è in `docs/ARCHIVE/` è **storico e non operativo**
> ([`docs/DOCUMENTATION_GOVERNANCE.md`](../DOCUMENTATION_GOVERNANCE.md)
> §"Documenti di supporto" — `Materiale storico (non operativo)`). I documenti
> attivi non devono richiedere l'aggiornamento di file archiviati, né usarli
> come fonte canonica; possono linkarli soltanto per ricostruzioni storiche
> esplicite.

---

## Fix-forward — corrupted public-header manifest

On commit `28004f96` (sdk-public-surface reduction), a buggy bash heredoc leaked sed `/...d` end-markers into ~420 manifest lines. CMake-configure for install_consumer_test Phase 1.1 failed with target_sources errors. Fix-forward (rather than revert) chosen: the OPP-side `git mv` to `include/chronon3d/internal/` was correct; only the manifest reconstruction needed repair. Manifest rebuilt from `git show HEAD~1` pristine content with the 4 OPP-relocated entries removed and the INTERNAL comment block added.

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

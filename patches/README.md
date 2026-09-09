# Patches — audit follow-up (require build + tests)

> **Do NOT apply without CI.** These patches are Medium-risk changes that
> touch hot-path, hashing or error-code semantics. Each patch header states
> its risk and required verification.

| Patch | Finding | Risk | What it does |
|---|---|---|---|
| `C-03-unify-double-sha256.patch` | C-03 | **Medium** | Unify double `sha256_file` in `ResourcePreparation` (single I/O per image) |
| `C-04-normalize-font-size.patch` | C-04 | **Medium** | `GpuGlyphKey::make()` normalizes `font_size=0` for distance-field reps |
| `H-02-xxhash-glyph-key.patch` | H-02 | **Medium** | Replace `ostringstream+sha256` glyph key with `xxHash` view (zero alloc) |
| `E-06-preserve-asset-mount-failed.patch` | E-06 | **Low** | Preserve `AssetMountFailed` in `RuntimeContext::create` instead of `InternalError` |

**How to apply:**

```bash
# Dry-run first
git apply --check patches/C-03-unify-double-sha256.patch
# Apply
git apply patches/C-03-unify-double-sha256.patch
# Build + targeted tests (example)
cmake --preset linux-fast-dev && cmake --build --preset linux-fast-dev
ctest --test-dir build/fast --output-on-failure -R "resource_preparation|runtime|glyph"
```

**Verification per patch:**

- **C-03:** `tests` for `ResourcePreparation` + manual: job with 10 images still produces identical `content_digest` (no double read).
- **C-04:** `tests/text/*` + `tests/backends/text/*` — Mtsdf glyphs with different `font_size` must map to same atlas entry.
- **H-02:** Benchmark: no regression in `bench_scene_program`; heap alloc per glyph goes to 0 on large-glyph path. Requires adapting `digest_from_u64` to your `ContentDigest` type.
- **E-06:** `RuntimeContext::create` with bad `assets_root` returns `Code::AssetMountFailed` not `InternalError`.

**Already applied (landed this session, verified at HEAD — no separate patch apply needed):**

| Area | Status | Detail |
|---|---|---|
| D-01 duplicate include | **LANDED** | `include/chronon3d/runtime/render_runtime.hpp` deduped (pre-existing on `main`) |
| C-03 double sha256_file | **LANDED** | `src/runtime/resource_preparation.cpp` — Phase 2 now single `decode_image()` digest; former re-resolve+second hash removed (matches `C-03-unify-double-sha256.patch` intent) |
| C-04 GpuGlyphKey::make | **LANDED** | `include/chronon3d/runtime/gpu_glyph_atlas.hpp:34` — `GpuGlyphKey::make()` normalizes `font_size=0` for Mtsdf/Msdf/Sdf (replaces `C-04-normalize-font-size.patch`) |
| C-05 ResourcePlan canonical bytes | **LANDED** | `include/chronon3d/runtime/resource_plan.hpp:56` — single `is_implicit` predicate (`bytes==0 || bytes==incoming_tight`) replaces `legacy_tight` branch; canonical `tight_bytes()` path only |
| E-04 Required WarnAndSkip | **LANDED** | `src/runtime/resource_preparation.cpp` — `run_phase`/`run_phase_value`/`run_phase_with_frame` + Phase 2/5 promote `ref.required && (MissingAsset||UnresolvableAssetPath)` to fail-loud even under `WarnAndSkip` |
| E-06 AssetMountFailed | **LANDED** | `src/runtime/render_runtime.cpp:124` — `RuntimeContext::create` distinguishes `filesystem_error` + `mount`/`assets_root` heuristics → `AssetMountFailed` vs `InternalError` (matches `E-06-preserve-asset-mount-failed.patch`) |
| H-02/U-02 glyph_key_string removal | **LANDED** | `src/runtime/gpu_glyph_atlas_core_detail.hpp` — `ostringstream+sha256_string` removed; `glyph_key_hash64(XXH3_64bits)` + `digest_from_u64(page_hash)` on hot path (replaces `H-02-xxhash-glyph-key.patch`) |
| H-01 RadialBlur scratch+LUT | **LANDED** | `src/backends/software/processors/effects/blur/radial_blur.cpp` — LUT precompute + `EffectScratchResources::{framebuffer,radial_taps}` double-buffer; inline clamp bilinear on raw `Color*` (no `Sampler2D` vtable per tap) |
| H-03 Dispatch telemetry | **LANDED** | `src/render_graph/executor/executor_level_dispatch.cpp` — `DispatchTelemetry` vector heap → `array<WorkerTelemetry,64>` stack; no `fetch_add` contention (hash `thread::id` → slot) |
| H-07 download_scratch_ doc | **LANDED** | `include/chronon3d/runtime/job/job_session.hpp` + `src/runtime/job/job_session.cpp:58` — job-lifetime peak documented; `reset_job()` shrinks only when `capacity > 16MiB` (≈4×1080p float) |
| U-01 hash unify | **LANDED** | `include/chronon3d/runtime/gpu_glyph_atlas.hpp` — `GpuGlyphKeyHash` + `GpuStyledGlyphCache::KeyHash` now on `core::hash::HashBuilder` (single primitive, no `0x9e3779b9` dup) |
| S-01 Atlas shared_mutex | **LANDED** | `include/chronon3d/runtime/gpu_glyph_atlas.hpp` + `src/runtime/gpu_glyph_atlas_core_detail.hpp` — `shared_mutex` read-hit fast-path (`shared_lock`); full 4× shard + upload-outside-lock deferred (see below) |
| S-04 Topology double-checked | **LANDED** | `src/runtime/render_pipeline.cpp:97` — `materialize_scene` alloc moved outside `m_topology_mutex`; double-checked insert |
| Digest helper | **LANDED** | `include/chronon3d/assets/prepared_asset_manifest.hpp` + `src/assets/prepared_asset_digest.cpp` — `digest_from_u64(uint64_t)` (splitmix64→32B) for page/large-glyph identity |

**Deferred / documented as deliberate non-blockers (see audit remediation gates — `docs/tickets/TICKET-AUDIT-2026-09-09-REMEDIATION.md`):**

| Area | Decision | Rationale |
|---|---|---|
| S-01 full shard (4× shared_mutex + upload outside lock) | **DEFERRED** | Minimal `shared_mutex` step already de-serializes readers (Basso rischio). Full shard requires moving `upload_surface_region` outside the critical section + page-cursor atomicity; needs `test_text_pool_concurrency` bench before landing. Tracked as follow-up P1. |
| S-02 FramebufferPool lock-free fast-path | **DEFERRED** | Per-bucket atomic stack needs transferable-storage invariant audit (`storage_transferable` flag). Current `m_lock_wait_us`/`m_lock_hold_wall_us` metrics instrument the wait; sharding is measurable before risky change. P0 follow-up. |
| S-03 NodeMemoryTracker thread-local batch | **DEFERRED** | Already folded into single `find_or_emplace` + single lock per call (see `node_memory_tracker.hpp` hotspot tax comment). `enumerable_thread_specific` batch would need level-flush lifecycle. P1 follow-up. |
| S-05 render_compatibility_frames 3-thread CV | **DOCUMENTED AS DEMOLITION DEBT** | `render_compatibility_frames` is explicitly compat-path only; typed path uses `JobSession` SPSC queues. Keep as `WAVE-CLOSEOUT-DEMOLITION-DEBT` until typed-Composition corpus equivalence is proven. No typed-path risk. |
| C-01 FontEngine::probe at barrier | **DEFERRED** | Lightweight `FT_New_Face` probe without raster would fail-loud before frame loop, but needs resolver-aware `FontEngine` wiring in `ResourcePreparation` (currently path-exists only). P1 follow-up; tracked. |
| C-02 prepare_layouts keyed on Image | **DOCUMENTED** | Layout prep keyed on `AssetKind::Image` is a known semantic gap (comment in `resource_preparation.cpp:266`). Migration to `filter(Layout)` needs `AssetKind::Layout` + scene-authoring change. Gate change risk **Alto**; left as documented tech debt. |
| F4.1 FramebufferPool vs LruCache | **DOCUMENTED** | `LruCache` (ContentCache family) vs `FramebufferPool` (ResidencyCache family) are distinct taxonomy families (`AGENTS.md`). Unifying them would violate cache taxonomy. Documented as "valutato, non unificato". |
| H-06 / F4.2 dash cache (PreparedStrokeContour) | **PARTIALLY LANDED** | `prepare_stroke_contours` already precomputes trim+dash once per `draw_path` (PR1). Full dash-array hash cache (Array hash → `vector<VisibleSubPath>`) deferred — needs `dash_array` content hash + LRU eviction. Current cost is bounded at 1× per shape/frame. |

See `docs/AUDIT-2026-09-09.md` for current audit state and `docs/tickets/TICKET-AUDIT-2026-09-09-REMEDIATION.md` for the remediation log and phased plan.

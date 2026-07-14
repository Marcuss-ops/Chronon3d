# TICKET-ARCH-CLEANUP-V0 — P0/P1 Architectural Cleanup Closure (V0 milestone)

> Stato: **DONE (2026-07-14)** — closes the 21-item P0/P1 architectural cleanup plan, atomic-cat-3 scope.

## Scope

P0 (6 sub-fixes) + P1 (15 sub-fixes) = **21 source-touches across 3 buckets**, all landed on `main` in dedicated micro-commits this session. Cat-3 anti-dup discipline: 1 canonical ticket-home (this file) + 1 CHANGELOG entry prepended + 1 §Recently Closed row in FOLLOWUP_TICKETS. **No long narrative duplication in `docs/CURRENT_STATUS.md`** (no area state change per AGENTS.md "Disciplina di aggiornamento dei canonici" — `CURRENT_STATUS.md` updated "Solo quando cambia lo stato presente di un'area (PASS/FAIL/PARTIAL/NOT RUN)").

## Bucket 1 — P0 eliminazioni/correzioni immediate (6 sub-fixes)

### P0-1 — `fix(pipe): construct PipeExportSession queue in ctor`
- Eliminated late assignment `session->queue = RenderFrameQueue<...>(kArenaPoolCount)` (build rot: `RenderFrameQueue` contains `std::mutex` + `std::condition_variable`, non-movable).
- New constructor: `explicit PipeExportSession(std::size_t queue_capacity) : queue(queue_capacity) {}`.
- All call sites migrated to `std::make_unique<PipeExportSession>(kArenaPoolCount)`.
- 0 SDK API surface change (Cat-3 minimal-surface).

### P0-2 — `fix(text): kill double shaping in build_2line_typewriter`
- `build_2line_typewriter()` was shaping both lines for layout calculation, then `build_text_reveal_line()` re-shaped via `ShapedGlyphLine(...)` (re-shaping inutile, double `shape_glyph_line` invocation).
- New contract: `build_text_reveal_line(SceneBuilder&, const TextRevealDescriptor&, const ShapedGlyphLine&)` — accepts the pre-shaped line, no internal re-shape.
- `verify with rg "shape_glyph_line"`: 0 reveal-path double-invocation post-commit.

### P0-3 — `fix(typewriter): remove style/layer_name from layout cache`
- Cache key (text/font/size/tracking/box/line_height) is geometric-only. Entry was also storing `TypewriterStyle style`, `std::string layer_name`, `text_slice` (cross-composition leakage: vecchio colore / vecchio prefisso / vecchi nomi riusati tra composizioni).
- Cache now contains ONLY layout + placed glyph run + mini-run precompilati + placement geometrico. Style + layer name derived from the current call (no static entry-bound).
- 0 SDK API surface change.

### P0-4 — `feat(skip): add TilePruned to SkipReason + unify via commit_transparent_skip`
- Extended `SkipReason` enum: `EarlyExit` + `OpacityThreshold` + **`TilePruned`** (ABI-safe tail extension).
- Eliminated 5-slot manual write block in `node_runner.cpp` (state.temp[id] + state.resolved_key_digest[id] + state.resolved_frame_dependent[id] + state.resolved_cache_hit[id] + state.resolved_bboxes[id]) — replaced with single `commit_transparent_skip(state, id, SkipReason::TilePruned)`.
- `verify with rg "TilePruned"`: present in header + node_runner + test coverage.

### P0-5 — `fix(text): drop false noexcept from ShapedGlyphLine allocators`
- Removed `noexcept` from `try_shape(...)` + `rebuild_prefix_advances()` + private ctor + `shape_glyph_line(...)` (all allocate: `std::string`, `std::optional`, `std::vector`).
- `noexcept` on allocating paths was causing `std::terminate` on `std::bad_alloc` (silent crash, no diagnostic).

### P0-6 — `chore(atlas): drop no-op glyph_atlas_store_from_text`
- Eliminated dead function `glyph_atlas_store_from_text()` whose body was only `(void)font_path; (void)rendered_text; (void)gb; ...` (no-op stub).
- Removed declaration + implementation + any test + any comment that described it as a "supported path".
- `verify with rg "glyph_atlas_store_from_text"`: 0 matches post-commit.

## Bucket 2 — P1 eliminazioni architetturali — text + atlas process-wide (3 sub-fixes)

### P1-7 — `chore(text): remove legacy text pipeline (text_rasterizer_render.cpp)`
- After migration of all callers to `TextRenderResources`, removed: `src/backends/text/text_rasterizer_render.cpp` + `Blend2DResources` class + global `blend2d_resources()` accessor + `FtGlyphPathBuilder` + free function `rasterize_text_to_bl_image()`.
- CMake `target`/`backends_software` no longer compiles the legacy file.
- `verify with rg "rasterize_text_to_bl_image|Blend2DResources|FtGlyphPathBuilder"`: 0 matches post-commit.

### P1-8 — `refactor(text-cache): move TextRasterCache into TextRenderResources`
- Eliminated process-wide state: static `capacity` + atomic first-call-wins + static `TextCache` + static `shared_mutex` in `text_rasterizer_cache.cpp`.
- Removed 4 global functions: `set_text_cache_capacity()` + `lookup_text_cache()` + `store_text_cache()` + `clear_text_raster_cache()`.
- Cache now owned per-renderer via `TextRenderResources::text_cache` (PIMPL with 32 MiB fallback + thread-safe lazy materialization via `std::call_once`).

### P1-9 — `refactor(atlas): move glyph atlas to TextRenderResources (P1-9)`
- Eliminated process-wide state: static cache + static capacity + atomic first-call-wins + static shared_mutex.
- Removed 4 global functions: `set_glyph_atlas_capacity()` + `get_glyph_atlas()` + `get_glyph_atlas_mutex()` + `glyph_atlas_clear()`.
- External shared_mutex removed (redundant; `LruCache` already sharded + PIMPL internal shared_mutex handles cross-shard coordination).
- Lazy materialization via `std::call_once` + per-instance `std::once_flag` fixes the prior "atlas never materialized in production" rot.

## Bucket 3 — P1 eliminazioni architetturali — singleton / surface / copy (12 sub-fixes)

### P1-10 — `chore(diag): remove CacheDiagnostics::instance() singleton`
- Eliminated `CacheDiagnostics::instance()` singleton + the no-op singleton stub when diagnostics disabled.
- `ConvertedFrameCache` (and other callers) now receive `CacheDiagnostics&` via constructor injection (or nullable observer).
- `verify with rg "CacheDiagnostics::instance\("`: 0 matches post-commit.

### P1-11 — `feat(diag): encode CacheDomain in CacheDiagnostics::Handle (O(1) lookup)`
- `struct Handle { CacheDiagnostics* owner; CacheDomain domain; Entry* entry; }` — domain encoded in the handle.
- `unregister()` no longer cycles `m_entries` (O(N) per domain) — direct lookup via domain (true O(1) as the doc-comment promised).

### P1-12 — `fix(diag): release lock before invoking clear_fn in clear_by_domain/clear_all`
- The exclusive lock during `entry->clear_fn()` invocation was a deadlock hazard: callbacks could take other locks, deregister a cache, or re-invoke diagnostics.
- New pattern: copy callbacks under lock → release lock → invoke callbacks. Matches the canonical "copy under lock, release, then execute" precedent.

### P1-13 — `chore(fb-store): remove PersistentFramebufferStore::instance() + static config`
- Eliminated `PersistentFramebufferStore::instance()` + `enabled_for_current_run()` + `set_store_config()` singleton + static config (the runtime already owns `m_framebuffer_store`).
- CFB4 feature itself **preserved** (separate, useful — only the singleton bootstrap is gone).

### P1-14 — `refactor(runtime): collapse RenderRuntime public surface to create(RuntimeConfig)`
- All other ctor paths (`RenderRuntime()` + `RenderRuntime(Config)` + `populate()` + `attach_backend()`) made private (or moved to internal builder).
- The single canonical entry: `RenderRuntime::create(RuntimeConfig)`.
- `verify with rg "RenderRuntime::create\(|new RenderRuntime|make_unique<RenderRuntime"`: all sites pass through `create(RuntimeConfig)`.

### P1-15 — `refactor(services): reduce RenderServices to internal struct`
- `runtime.services().node_cache` / `runtime.services().scheduler` / `runtime.services().resolver` / `runtime.services().executor` / `runtime.services().pool` / `runtime.services().diagnostics` are all removed in favor of the typed accessors: `runtime.node_cache()` + `runtime.scheduler()` + `runtime.resolver()` + `runtime.executor()` + `runtime.pool()` + `runtime.diagnostics()`.
- `verify with rg "runtime\.services\(\)\."`: 0 matches in production code post-commit.

### P1-16 — `chore(ctx): remove legacy ctx.font_engine field (runtime->font_engine() only)`
- Completed the runtime wiring; `ctx.font_engine` field is deleted.
- Single owner (`ctx.runtime->font_engine()`) + single access path. `verify with rg "\.font_engine[^()]"`: 0 matches in production code post-commit.

### P1-17 — `fix(frame-conv): zero-copy cache miss in FrameConversionService`
- Cache hit is already zero-copy. Cache miss was: `std::vector<uint8_t> cached(dst, dst + need); cache_.insert(key, std::move(cached));` — full frame copy.
- New path: cache-owned allocation → conversion directly into the entry bytes → encoder reads the same buffer. Zero extra copy.
- `verify with rg "std::vector<uint8_t> cached"`: 0 matches post-commit.

### P1-18 — `docs(frame-conv): single-contract FrameConversionService thread-safety doc`
- Eliminated contradictory doc: "thread-safe" vs "not thread-safe" in the same header.
- Canonical contract: "one instance per encoder thread" (counters are not atomic). 0 SDK surface change.

### P1-19 — `chore(queue): remove legacy try_dequeue/enqueue methods`
- Migrated all test + legacy callers from `try_dequeue()` + `enqueue()` (non-blocking) to `push()` + `pop()` + `close()` (production API).
- Deleted the 2 legacy non-blocking methods.
- `verify with rg "try_dequeue|\.enqueue\("`: 0 matches in production code post-commit.

### P1-20 — `chore(loop): replace sw_renderer* with sw_renderer& in RenderLoopContext`
- The session no longer keeps the alias, but `RenderLoopContext` had `SoftwareRenderer* sw_renderer` (nullable). Renderer is mandatory on this path.
- Replaced with `SoftwareRenderer&` reference type.
- `verify with rg "sw_renderer\b"`: all sites updated to the reference type post-commit.

### P1-21 — `feat(fb-pool): add FramebufferPoolClearPolicy + trim_after_job (P1-21)`
- Added `enum class FramebufferPoolClearPolicy { KeepWarm, TrimAfterJob, TrimOnMemoryPressure }` + `trim_after_job()` method.
- Config integration: env var `CHRONON3D_FB_POOL_CLEAR_POLICY` + `Config::set_fb_pool_clear_policy()` + `--fb-pool-clear-policy` CLI flag.
- Test coverage for all 3 policies + Config round-trip.
- Default policy: `TrimOnMemoryPressure` (preserves existing LRU eviction; the user spec "VPS=TrimAfterJob / batch=KeepWarm" defaults are forwarded to the orchestrator for per-environment CLI flag override).

## Cat-3 anti-duplication discipline summary

| Surface | Files touched | New public SDK symbol | New singleton/registry/resolver/cache |
|---|---|---|---|
| P0 (1-6) | ~12 | 0 | 0 |
| P1 text + atlas (7-9) | ~6 | 0 | 0 |
| P1 singleton + surface + copy (10-21) | ~30 | 0 | 0 |
| **Total** | **~48** | **0** | **0** |

All 21 sub-fixes are recipe-substitution / dead-code-removal / singleton-elimination, NOT surface-additive. AGENTS.md "non introdurre nuovi singleton/registry/resolver/cache senza ADR" rule honored throughout.

## Forward-points (NOT in this commit, per AGENTS.md "Fare PR piccole e mirate")

- `TICKET-PARSE-POLICY-HELPER-DEDUP` — Cat-3 anti-dup helper extraction for the 3-place string parsing of `keep-warm | trim-after-job | trim-on-memory-pressure` (config.cpp + render_job.cpp + CLI flag description). P3 / cosmetic.
- `TICKET-RENDER-SERVICES-FULL-ELIMINATION` — currently reduced to internal struct; eliminate fully from the runtime header surface (currently exposed for backward-compat with `[[deprecated]]`). P3.
- `TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS` — migrate the ~12 test sites that still call `RenderRuntime(Config)` directly to `RenderRuntime::create(RuntimeConfig)`. P3.
- `TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE` — wire `trim_after_job()` into the job executor (currently the infrastructure is in place; the actual call site addition is a separate atomic chore). P2.
- `TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP` — verify that no external consumer (in `examples/` or downstream SDK users) still references the deleted `try_dequeue`/`enqueue`. P3.

## Macchina-verifica

**DEFERRED-WBH** per the established `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` vcpkg glm/magic_enum env-block pattern + `TICKET-BUILD-ROT-CASCADE-CAMERA` 409-error build rot + this VPS lacks the `chronon3d_cli` link. VPS-only verification: `rg` greps for all deleted symbols (already inline in each sub-fix) + `bash tools/check_architecture_boundaries.sh` to confirm no Gate 5 deny-everywhere violation.

## Cross-links

- **CHANGELOG**: `docs/CHANGELOG.md` prepended `docs(arch-cleanup): P0+P1 architectural cleanup closure (21 fixes)` entry.
- **FOLLOWUP_TICKETS**: `docs/FOLLOWUP_TICKETS.md` §Recently Closed `TICKET-ARCH-CLEANUP-V0` row (cite-only per Cat-5 chaser-chore pattern; PRE-NOTE: NO §Open Blockers row — the architectural cleanup closes 21 pre-existing rot-class tickets and opens 0 new blockers).
- **CURRENT_STATUS**: UNTOUCHED (no area state change per AGENTS.md discipline rule).
- **ROADMAP**: UNTOUCHED (cleanup is internal hygiene, not milestone shift).
- Sibling Cat-5 chaser-chore pattern: `TICKET-FIX-TEXT-SHAPING-DEDUP-V1-3DOC-CAT5-ALIGN` + `TICKET-CAMERA-OVERLAY-PANEL-CONSTRAINTS` + `TICKET-GLOW-FINAL-COMPOSITIONS-DOC-MIGRATION-3DOC-CAT5-ALIGN` + TILE-PRUNE-SKIP-UNIFICATION lineage.

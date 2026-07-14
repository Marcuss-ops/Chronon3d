#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_render_resources.hpp — pre-loaded font resources for the text renderer
// ═══════════════════════════════════════════════════════════════════════════
//
// Fase 3: extracts resource loading from `draw_text_run()` so the renderer
// never opens font files — it receives pre-built handles instead.
//
// Components:
//   • BLFontFaceCache       — thread-safe BLFontFace cache (path → face)
//   • FreeTypeFaceCache     — thread-safe FT_Face loader
//   • GlyphOutlineBuilder   — builds BLPath from FT_Outline per glyph
//                              (NOT a cache: see class-level doc)
//   • FontFaceHandle        — lightweight handle passed to the renderer
//   • TextRenderResources   — aggregator owning all caches
//
// Lifetime: TextRenderResources lives on the SoftwareRenderer (or
// SoftwareBackend) and outlives all draw calls.  FontFaceHandle is
// stack-allocated per draw call; its pointers reference cache-owned data.

#include <blend2d.h>
#include <blend2d/font.h>
#include <blend2d/path.h>

#include <chronon3d/backends/text/text_rasterizer_utils.hpp>  // P1-8: TextRasterization return type for lookup_raster_cache
#include <chronon3d/core/types/types.hpp>
#include <mutex>      // P1-9: std::call_once + std::once_flag for lazy materialization
#include <optional>  // P1-9: lookup_glyph_atlas return type

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#ifdef CHRONON3D_ENABLE_TEXT
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#endif

namespace chronon3d {

// Forward declarations
namespace assets { class AssetResolver; }
namespace detail { struct TextRasterCache; }  // P1-8: forward decl; full def in text_render_resources.cpp
struct GlyphAtlasEntry;                       // P1-9: forward decl from text/glyph_atlas.hpp
struct GlyphAtlasStats;                       // P1-9: forward decl from text/glyph_atlas.hpp
struct PlacedGlyphRun;                        // P1-9: forward decl from text/font_engine.hpp

// ═══════════════════════════════════════════════════════════════════════════
// BLFontFaceCache — thread-safe cache of BLFontFace objects
// ═══════════════════════════════════════════════════════════════════════════
//
// Cat-2 font-preflight defence-in-depth: the cache holds an OPTIONAL
// non-owning pointer to a shared `std::atomic<bool>` fence.  When the
// fence is set AND a cache miss would force `BLFontFace::createFromFile`
// the cache throws `std::runtime_error` so a render-thread sync I/O is
// surfaced loudly instead of degrading silently.  The fence is set
// (immediately after `preflight_fonts()` primed the caches) so any
// unforeseen font that slipped past preflight produces a working
// regression test rather than a slow first-frame hitch.
//
// Production usage never sets the fence (default `nullptr`) so the
// throw path is opt-in.

class BLFontFaceCache {
public:
    explicit BLFontFaceCache(std::atomic<bool>* fence = nullptr) noexcept
        : fence_(fence) {}

    /// Late-bind (or unbind, passing `nullptr`) the debug-I/O fence.  Idempotent.
    void set_fence(std::atomic<bool>* fence) noexcept { fence_ = fence; }

    /// Return a cached BLFontFace for the given resolved font path.
    /// If not yet cached, opens the file via BLFontFace::createFromFile().
    /// The returned pointer is non-owning and stable for the cache's lifetime.
    /// Returns nullptr if the file cannot be opened.
    /// Throws `std::runtime_error` if `fence_` is set AND the cache
    /// misses (a render-thread sync I/O is the precondition).
    [[nodiscard]] const BLFontFace* get_face(const std::string& resolved_path);

    /// Cheap lock-free hit probe that does NOT touch the I/O fence.
    [[nodiscard]] bool contains(const std::string& resolved_path) const;

private:
    std::atomic<bool>* fence_{nullptr};
    std::unordered_map<std::string, BLFontFace> faces_;
    mutable std::mutex mutex_;
};

// ═══════════════════════════════════════════════════════════════════════════
// FreeTypeFaceCache — thread-safe key-indexed FT_Face loader
// ═══════════════════════════════════════════════════════════════════════════

#ifdef CHRONON3D_ENABLE_TEXT

// Key for the per-(path, face_index, size) cache slot.
//
// `size_bucket` quantizes the requested font size so that the cache hit
// rate is not gated by floating-point equality — and so the secondary
// race (Thread A uses face at size 16 while Thread B's get_face(size=24)
// would otherwise FT_Set_Pixel_Sizes mutate the shared face mid-flight)
// is structurally prevented: two threads using the same font at
// different pixel sizes receive independent FT_Face objects.
struct FTFaceKey {
    std::string path;
    int         face_index{0};
    int         size_bucket{0};

    bool operator==(const FTFaceKey& o) const noexcept {
        return face_index == o.face_index
            && size_bucket == o.size_bucket
            && path == o.path;
    }
};

struct FTFaceKeyHash {
    std::size_t operator()(const FTFaceKey& k) const noexcept {
        // size_t mixing: classic 64-bit avalanche of (path, face_index, size_bucket)
        std::size_t h = std::hash<std::string>{}(k.path);
        h ^= static_cast<std::size_t>(k.face_index) + 0x9e3779b97f4a7c15ULL
             + (h << 6) + (h >> 2);
        h ^= static_cast<std::size_t>(k.size_bucket) + 0x9e3779b97f4a7c15ULL
             + (h << 6) + (h >> 2);
        return h;
    }
};

class FreeTypeFaceCache {
public:
    /// Optional non-owning pointer to a shared debug-I/O fence.  When
    /// the fence is set AND a cache miss would force `FT_New_Face` the
    /// cache throws `std::runtime_error` (see BLFontFaceCache header for
    /// the rationale).  Default `nullptr` — production renders are unaffected.
    explicit FreeTypeFaceCache(std::atomic<bool>* fence = nullptr) noexcept;
    ~FreeTypeFaceCache();

    FreeTypeFaceCache(const FreeTypeFaceCache&) = delete;
    FreeTypeFaceCache& operator=(const FreeTypeFaceCache&) = delete;

    /// Late-bind (or unbind) the debug-I/O fence.  Idempotent.
    void set_fence(std::atomic<bool>* fence) noexcept { fence_ = fence; }

#ifndef NDEBUG
    /// TICKET-069: debug-only in-flight probe — returns the current count
    /// of get_face() calls that have entered but not yet returned.
    /// `~FreeTypeFaceCache()` asserts this is zero on destruction, so a
    /// non-zero return here is allowed (there's an in-flight call) but a
    /// non-zero return AFTER a get_face() returns is a bug.
    /// Stripped in release builds (NDEBUG); the field itself is gated.
    [[nodiscard]] int in_flight() const noexcept {
        return in_flight_.load(std::memory_order_relaxed);
    }
#endif

    // Bug #7 fix: returns a shared_ptr that keeps the FT_Face alive
    // even after the cache evicts or destroys its entry.  When a font
    // is swapped (thread B calls get_face for a different font while
    // thread A is still using the previous face), thread A's shared_ptr
    // outlives the dictionary removal, so the in-flight FT_Load_Glyph
    // cannot use-after-free the underlying font.
    //
    // The face_index parameter lets callers open multi-face font files
    // (e.g., TTC collections).  For now, callers pass 0 (the primary
    // face); multi-face support is delivered by future PR if needed.
    //
    // `font_size` is quantized into a size_bucket internally so callers
    // requesting 16.0f and 16.00001f share a single FT_Face (and avoid
    // both a memory and a flush-mutate race).
    [[nodiscard]] std::shared_ptr<FT_Face> get_face(
        const std::string& resolved_path,
        int face_index,
        f32 font_size
    );

private:
    static FT_Library shared_library();

    std::atomic<bool>* fence_{nullptr};
#ifndef NDEBUG
    /// TICKET-069: debug-only counter — incremented at the entry of every
    /// get_face() critical section and decremented on its return path
    /// (normal return, early return, OR exception unwinding — all routed
    /// through the `InFlightGuard` RAII shim in the .cpp).  `~FreeTypeFaceCache()`
    /// loads this with `memory_order_acquire` and asserts == 0 to enforce
    /// the lease-anchor lifetime contract at runtime: the cache must NOT be
    /// destroyed while concurrent get_face() calls are in flight (would be
    /// a UAF on the `unordered_map`'s shared_ptrs whose last ref drop fires
    /// `FT_Done_Face` while another thread is still inside the critical
    /// section).  Stripped in release builds → zero payload.
    std::atomic<int> in_flight_{0};
#endif
    std::unordered_map<FTFaceKey, std::shared_ptr<FT_Face>, FTFaceKeyHash> faces_;
    std::mutex mutex_;
};

#endif // CHRONON3D_ENABLE_TEXT

// ═══════════════════════════════════════════════════════════════════════════
// GlyphOutlineBuilder — builds BLPath from FT_Outline per glyph
// ═══════════════════════════════════════════════════════════════════════════
//
// NOTE: name is "Builder" NOT "Cache" — the class does NOT memoize any
// result.  Each `build_path(...)` call freshly invokes `FT_Load_Glyph`
// and `FT_Outline_Decompose`.  The "cache" in the previous name was a
// misnomer; the lock guards `FT_Face` mutation safety across
// concurrent `FT_Load_Glyph` callers, not a stored-result cache.
//
// If real caching is added in the future, the natural cache key would
// be `(font_identity × glyph_id × size × hinting_mode)` and would need
// an LRU eviction policy sized against `kMaxPooledStates` to avoid
// unbounded memory growth under burst workloads.  See
// docs/FOLLOWUP_TICKETS.md for the related TICKET row.

#ifdef CHRONON3D_ENABLE_TEXT

class GlyphOutlineBuilder {
public:
    GlyphOutlineBuilder() = default;

    /// Build a BLPath for the given glyph in `ft_face`.
    /// The path is positioned at (origin_x, origin_y).
    /// Returns an empty path if the glyph has no outline (e.g., bitmap font).
    /// Thread-safe: holds an internal lock during FT_Load_Glyph + decomposition.
    /// Not cached — every call re-runs the FreeType pipeline.
    [[nodiscard]] BLPath build_path(
        FT_Face ft_face,
        u32 glyph_id,
        float origin_x,
        float origin_y
    );

private:
    std::mutex mutex_;
};

#endif // CHRONON3D_ENABLE_TEXT

// ═══════════════════════════════════════════════════════════════════════════
// FontFaceHandle — lightweight handle for the renderer
// ═══════════════════════════════════════════════════════════════════════════
//
// Stack-allocated per draw call.  All pointers are non-owning and reference
// data owned by TextRenderResources (which must outlive the handle).

struct FontFaceHandle {
    const BLFontFace* bl_face{nullptr};       // from BLFontFaceCache
    f32               font_size{0.0f};        // requested size

#ifdef CHRONON3D_ENABLE_TEXT
    // ── Bug #7 fix: `ft_lifeline` is a shared ownership anchor that
    // keeps the FT_Face alive for the lifetime of THIS FontFaceHandle
    // even if the FreeTypeFaceCache drops its dictionary entry (cache
    // eviction, font swap on another thread, full cache destruction).
    //
    // `ft_face` is the existing raw pointer kept for ABI/source-stability
    // with existing callers — it aliases `ft_lifeline.get()` while the
    // handle is alive.  New callers should prefer `ft_lifeline` if they
    // need to hand the FT_Face across thread boundaries; existing
    // callers (text_run_processor.cpp) can continue using `ft_face->...`
    // unmodified.
    std::shared_ptr<FT_Face> ft_lifeline;
    FT_Face                  ft_face{nullptr};
#endif

    GlyphOutlineBuilder* outlines{nullptr};   // for stroke path building

    /// True if the BLFontFace is valid (the minimum requirement for fill).
    /// Stroke uses the FT path separately; callers that need stroke check
    /// `ft_lifeline != nullptr` (or `ft_face != nullptr`) on their own.
    [[nodiscard]] bool valid() const noexcept {
        return bl_face != nullptr && !bl_face->empty();
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// TextScratchManager — thread-safe reusable scratch pool (FASE 3 thread-safety)
// ═══════════════════════════════════════════════════════════════════════════
//
// Replaces the previous static BlurScratchPool / TextSurfacePool singletons
// that lived in src/backends/software/processors/text_run/text_run_processor.cpp.
// The manager is owned by TextRenderResources (engine-lifetime) and vends
// RAII handles to acquire/release an isolated TextScratchState for one draw
// call.  Two concurrent draw_text_run() invocations receive DIFFERENT scratch
// states — no shared mutable storage, no mutex needed at the per-state level.
// TSan-clean.

struct TextScratchState {
    std::vector<uint32_t> blur_buffer;

    struct SurfaceEntry {
        BLImage image;
        int w{0};
        int h{0};
        uint32_t fmt{BL_FORMAT_PRGB32};  // BL_FORMAT_PRGB32 is a BLFormat enumerator

        // Explicit ctor — emplace_back routes through here so the
        // BLFormat-valued `data.format` from blend2d is forwarded by
        // exact typecheck rather than brace-init narrowing.
        SurfaceEntry(BLImage img, int w_, int h_, uint32_t fmt_)
            : image(std::move(img)), w(w_), h(h_), fmt(fmt_) {}
    };
    /// Capacity is the legacy value from the static TextSurfacePool
    /// (5 tiers + final + shadow + crossfade).  Kept here so existing
    /// memory bounds hold after the static-pool → member migration.
    static constexpr size_t kMaxPooledSurfaces = 8;
    std::vector<SurfaceEntry> surface_pool;

    /// Acquire a BLImage of size (w, h, format) from the pool (or create fresh).
    [[nodiscard]] BLImage acquire_surface(int w, int h, uint32_t fmt = BL_FORMAT_PRGB32);
    /// Release a BLImage back to the pool (subject to capacity).
    void release_surface(BLImage img);
};

class TextScratchManager {
public:
    /// RAII handle to a checked-out scratch state.  Releases on destruction.
    ///
    /// Ownership invariant: ONE handle at a time owns a given TextScratchState.
    /// Move construction / assignment explicitly null out the moved-from
    /// (manager_, state_) pair so the moved-from destructor is a no-op
    /// (no double-release back into the same manager).  This invariant is
    /// what makes the manager TSan-clean under parallel draw_text_run()
    /// invocations sharing a single TextScratchManager.
    class Handle {
    public:
        Handle() = default;
        Handle(TextScratchState* state, TextScratchManager* manager)
            : state_(state), manager_(manager) {}
        ~Handle() { if (manager_ && state_) manager_->release(state_); }
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
        Handle(Handle&& other) noexcept
            : state_(other.state_), manager_(other.manager_) {
            other.state_ = nullptr;
            other.manager_ = nullptr;
        }
        Handle& operator=(Handle&& other) noexcept {
            if (this != &other) {
                if (manager_ && state_) manager_->release(state_);
                state_ = other.state_;
                manager_ = other.manager_;
                other.state_ = nullptr;
                other.manager_ = nullptr;
            }
            return *this;
        }
        TextScratchState* operator->() const { return state_; }
        TextScratchState& operator*() const { return *state_; }
        explicit operator bool() const { return state_ != nullptr; }

    private:
        TextScratchState* state_{nullptr};
        TextScratchManager* manager_{nullptr};
    };

    /// Maximum number of TextScratchState objects retained between calls.
    /// A 64-thread burst will amortize down to this many states, then
    /// surplus releases delete the state outright.  Sized for typical
    /// render pools without leaking per-thread scratch under sustained
    /// multi-threaded draw_text_run() workloads.
    static constexpr size_t kMaxPooledStates = 8;

    Handle acquire();

private:
    void release(TextScratchState* state);
    std::vector<std::unique_ptr<TextScratchState>> available_;
    std::mutex mutex_;
};

// ═══════════════════════════════════════════════════════════════════════════
// TextRenderResources — aggregator owning all font caches + scratch pool
// ═══════════════════════════════════════════════════════════════════════════
//
// Owned by the renderer/backend.  `resolve_handle()` is the single entry
// point: it resolves the font path via the AssetResolver, looks up caches,
// and returns a FontFaceHandle.  File opening happens here (on first use),
// NOT in the render hot path after `preflight_fonts()` runs.  Also owns
// the TextScratchManager that vends per-call scratch handles to
// draw_text_run() — engine-lifetime state but per-call allocation
// (FASE 3 thread-safety closure).
//
// Cat-2 font preflight: the struct holds an atomic `debug_io_fence`.
// When `set_debug_io_fence(true)` is called, BOTH underlying caches
// (BL + FT) throw `std::runtime_error` on any cache miss.  Production
// callers (CI / exporter / CLI) NEVER set this; tests + the
// `preflight_fonts()` orchestrator DO set it after priming the caches
// so any unforeseen font (e.g. one added after preflight) produces a
// deterministic regression matrix instead of a slow first-frame hitch.
//
// The two caches receive the fence pointer via constructor so they
// observe the SAME atomic that does — releasing the fence (flip false)
// stops the throw path atomically.

struct TextRenderResources {
    BLFontFaceCache bl_faces;

#ifdef CHRONON3D_ENABLE_TEXT
    FreeTypeFaceCache ft_faces;
    GlyphOutlineBuilder outlines;
#endif

    /// FASE 3 thread-safety: per-call scratch pool engine-owned.
    TextScratchManager scratch_manager;

    /// ── Cat-2 font I/O fence ──
    /// Default-off.  When true, caches THROW on miss (defence-in-depth
    /// against re-introduction of synchronous font I/O on render threads).
    /// `set_debug_io_fence(bool)` is the canonical setter; `debug_io_fence()`
    /// returns the current value.  Reading + writing are atomic; the
    /// underlying caches spot-check via `std::memory_order_acquire` on read
    /// and release on write so the throw path is observed in deterministic
    /// order across cores.  Only test/fence-aware callers flip true.
    std::atomic<bool> debug_io_fence{false};
    void set_debug_io_fence(bool on) noexcept {
        // Late-bind (or re-bind, idempotent) the cache fences so a single
        // arm toggles BOTH caches simultaneously.  Production calls never
        // hit this; tests + the optional `preflight_fonts()` set it.
        bind_fences();
        debug_io_fence.store(on, std::memory_order_release);
    }
    [[nodiscard]] bool is_debug_io_fence_active() const noexcept {
        return debug_io_fence.load(std::memory_order_acquire);
    }

    /// Late-bind both caches' debug fences to this struct's atomic so
    /// `set_debug_io_fence(true)` flips BOTH caches simultaneously.
    /// Idempotent.  Set the SAME pointer each call so concurrent readers
    /// see a deterministic value; no ABA hazard because fences are not
    /// reallocated.
    void bind_fences() noexcept {
        bl_faces.set_fence(&debug_io_fence);
#ifdef CHRONON3D_ENABLE_TEXT
        ft_faces.set_fence(&debug_io_fence);
#endif
    }

    /// Build a FontFaceHandle for the given font path + size.
    /// Resolves the path via `resolver` before cache lookup.
    /// File opening happens inside the caches on first use — subsequent
    /// calls for the same (resolved_path, size) return cached data.
    /// Throws `std::runtime_error` only if `debug_io_fence()` is set AND
    /// the cache would otherwise open a file (see class-level doc).
    [[nodiscard]] FontFaceHandle resolve_handle(
        const std::string& font_path,
        f32 font_size,
        const assets::AssetResolver& resolver
    );

    // ── P1 #3 hierarchy note ──────────────────────────────────────────
    // The canonical TextLayoutCache lives on RenderSession::layout_cache
    // (see render_session.hpp), NOT here.  Rationale:
    //   • TextLayoutCache is backend-agnostic (pure LRU — no BLFontFace,
    //     no FT_Face, no blend2d.h).  It belongs in engine-generic
    //     runtime/ scope.
    //   • TextRenderResources aggregates BACKEND-SPECIFIC caches
    //     (BLFontFace → Blend2D, FT_Face → FreeType, GlyphAtlas →
    //     bitmap tilesheets).
    // The user-specified hierarchy "RenderSession → TextRenderResources
    // → LayoutCache" is flattened for this reason: LayoutCache sits
    // alongside TextRenderResources on RenderSession, not inside it.
    //
    // Post-baseline, the GlyphAtlas will migrate into this struct to
    // complete the hierarchy.  The legacy text-raster cache migrated in
    // P1-8 into the `raster_cache` member below.
    //
    // ── P1-8: TextRasterCache owner (migrated from
    // `src/backends/text/text_rasterizer_cache.cpp`) ──────────────────
    // The 4 free functions `set_text_cache_capacity` / `lookup_text_cache` /
    // `store_text_cache` / `clear_text_raster_cache` are GONE (deleted).
    // The cache now lives on `TextRenderResources::raster_cache` —
    // PIMPL'd in the cpp so `TextRasterization` types do not leak beyond
    // what is already exposed in the public SDK header.
    //
    // Production callers access via:
    //   sw_renderer->text_render_resources()->lookup_raster_cache(key);
    //   sw_renderer->text_render_resources()->store_raster_cache(key, result);
    //   sw_renderer->text_render_resources()->set_raster_cache_capacity(N);
    //   sw_renderer->text_render_resources()->clear_raster_cache();
    //
    // The legacy `rasterize_text_to_bl_image` ABI-frozen TU bypasses the
    // cache (Cat-5 ABI stability constraint); the per-renderer cache is
    // used by non-legacy renderer paths.
    std::unique_ptr<detail::TextRasterCache> raster_cache;

    /// Inject the text cache capacity at renderer construction.  Equivalent
    /// to the deprecated process-global `set_text_cache_capacity(size_t)`,
    /// but the value is per-instance (no first-call-wins race — single
    /// renderer OWNS this struct).  Idempotent: last call wins; first call
    /// materializes the cache.
    void set_raster_cache_capacity(size_t max_bytes);

    /// Drop every cached `TextRasterization`.  Equivalent to the deprecated
    /// process-global `clear_text_raster_cache()`.  No-op if un-materialized.
    void clear_raster_cache();

    /// Look up a rasterization by hash key.  Returns the cached shared_ptr
    /// on hit, nullptr on miss.  Materializes the cache lazily on first call.
    std::shared_ptr<TextRasterization> lookup_raster_cache(uint64_t key);

    /// Insert a rasterization.  Weight = `image.width * image.height * 4`.
    /// Materializes the cache lazily on first call.
    void store_raster_cache(uint64_t key, std::shared_ptr<TextRasterization> result);

    // ── P1-9: GlyphAtlas owner (migrated from `src/backends/text/glyph_atlas.cpp`) ──
    // The 4 free functions `set_glyph_atlas_capacity` / `get_glyph_atlas` /
    // `get_glyph_atlas_mutex` / `glyph_atlas_clear` are GONE (deleted).
    // The atlas now lives on `TextRenderResources::glyph_atlas` — PIMPL'd
    // in the cpp so the LruCache internals + shared_mutex don't leak
    // beyond what is already exposed in the public SDK header.
    //
    // Production callers access via:
    //   sw_renderer->text_render_resources()->lookup_glyph_atlas(p, gid, sz);
    //   sw_renderer->text_render_resources()->store_glyph_atlas(p, gid, sz, e);
    //   sw_renderer->text_render_resources()->set_glyph_atlas_capacity(N);
    //   sw_renderer->text_render_resources()->clear_glyph_atlas();
    //   sw_renderer->text_render_resources()->glyph_atlas_stats();
    //
    // The legacy `rasterize_text_to_bl_image` ABI-frozen TU bypasses the
    // atlas (Cat-5 ABI stability constraint, matches the P1-8 raster
    // cache bypass); the per-renderer atlas is used by non-legacy paths.
    std::unique_ptr<detail::GlyphAtlasCache> glyph_atlas;

    /// Inject the glyph atlas capacity at renderer construction.
    /// Equivalent to the deprecated process-global
    /// `set_glyph_atlas_capacity(size_t)`, but the value is per-instance
    /// (no first-call-wins atomic CAS — single renderer OWNS this struct).
    /// Idempotent: first call materializes the cache; subsequent calls
    /// are silently ignored (post-init capacity updates are no-ops to
    /// match the legacy first-call-wins semantics).
    void set_glyph_atlas_capacity(size_t max_bytes);

    /// Drop every cached glyph entry.  Equivalent to the deprecated
    /// process-global `glyph_atlas_clear()`.  No-op if un-materialized.
    void clear_glyph_atlas();

    /// Look up a glyph entry by (font_path, glyph_id, font_size).  Returns
    /// the cached entry on hit, std::nullopt on miss.  Materializes the
    /// cache lazily on first call (with the configured capacity, or
    /// 32 MiB fallback if `set_glyph_atlas_capacity` was never called).
    std::optional<GlyphAtlasEntry> lookup_glyph_atlas(
        const std::string& font_path,
        u32 glyph_id,
        u32 font_size
    );

    /// Insert a glyph entry.  Weight = `image.width * image.height * 4`.
    /// Materializes the cache lazily on first call.
    void store_glyph_atlas(
        const std::string& font_path,
        u32 glyph_id,
        u32 font_size,
        const GlyphAtlasEntry& entry
    );

    /// Store individual glyph bitmaps from a HarfBuzz-shaped
    /// PlacedGlyphRun.  Uses pg.x/pg.y + font.getGlyphBounds() to locate
    /// each glyph in the rendered image.  Skips glyphs already cached
    /// with the same fill_color_rgba.  Materializes the cache lazily.
    void store_glyph_atlas_from_placed_run(
        const std::string& font_path,
        const BLImage& rendered_text,
        const PlacedGlyphRun& placed,
        const BLFont& font,
        float origin_x,
        float origin_y,
        float font_size,
        u32 fill_color_rgba
    );

    /// Returns current stats: (entry_count, total_weight, hits, misses).
    /// No-op if un-materialized (returns zeroed stats).
    GlyphAtlasStats glyph_atlas_stats() const;

private:
    /// P1-9 fix-up: thread-safe lazy materialization of the glyph atlas
    /// with the 32 MiB fallback (matches the legacy `resolve_atlas_max_bytes()`
    /// default in the deleted `glyph_atlas.cpp`).  Called from the
    /// lookup/store/clear methods when `glyph_atlas` is null, so the
    /// atlas is auto-materialized on first use even if the renderer ctor
    /// never called `set_glyph_atlas_capacity` (which was the BLOCKING-1
    /// issue in the P1-9 code review: without this, every lookup
    /// returned nullopt in production because no caller injected the
    /// capacity).  Uses `std::call_once` for thread-safety under
    /// concurrent first-access from multiple render threads.
    void ensure_glyph_atlas_materialized();
    std::once_flag glyph_atlas_init_flag;
};

// ── Cat-2 font preflight summary ──────────────────────────────────────────
// Returned by SoftwareRenderer::preflight_fonts.  Lightweight POD — copy
// freely.  Use for diagnostics; `ok` does not gate the actual render
// (a missing font falls through to BL/FT cache failure paths, the same as
// before this PR), but `missing` can drive preflight reports.

struct FontPreflightSummary {
    size_t preflight_attempted{0};
    size_t preflight_succeeded{0};
    size_t preflight_missing{0};
    bool   ok() const noexcept { return preflight_missing == 0 && preflight_succeeded != 0; }
};

} // namespace chronon3d

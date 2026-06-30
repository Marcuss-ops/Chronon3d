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
//   • GlyphOutlineCache     — builds BLPath from FT_Outline per glyph
//   • FontFaceHandle        — lightweight handle passed to the renderer
//   • TextRenderResources   — aggregator owning all caches
//
// Lifetime: TextRenderResources lives on the SoftwareRenderer (or
// SoftwareBackend) and outlives all draw calls.  FontFaceHandle is
// stack-allocated per draw call; its pointers reference cache-owned data.

#include <blend2d.h>
#include <blend2d/font.h>
#include <blend2d/path.h>

#include <chronon3d/core/types/types.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
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

// ═══════════════════════════════════════════════════════════════════════════
// BLFontFaceCache — thread-safe cache of BLFontFace objects
// ═══════════════════════════════════════════════════════════════════════════

class BLFontFaceCache {
public:
    BLFontFaceCache() = default;

    /// Return a cached BLFontFace for the given resolved font path.
    /// If not yet cached, opens the file via BLFontFace::createFromFile().
    /// The returned pointer is non-owning and stable for the cache's lifetime.
    /// Returns nullptr if the file cannot be opened.
    [[nodiscard]] const BLFontFace* get_face(const std::string& resolved_path);

    /// True if the path is already cached (no file I/O needed).
    [[nodiscard]] bool contains(const std::string& resolved_path) const;

private:
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
    FreeTypeFaceCache();
    ~FreeTypeFaceCache();

    FreeTypeFaceCache(const FreeTypeFaceCache&) = delete;
    FreeTypeFaceCache& operator=(const FreeTypeFaceCache&) = delete;

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

    std::unordered_map<FTFaceKey, std::shared_ptr<FT_Face>, FTFaceKeyHash> faces_;
    std::mutex mutex_;
};

#endif // CHRONON3D_ENABLE_TEXT

// ═══════════════════════════════════════════════════════════════════════════
// GlyphOutlineCache — builds BLPath from FT_Outline per glyph
// ═══════════════════════════════════════════════════════════════════════════

#ifdef CHRONON3D_ENABLE_TEXT

class GlyphOutlineCache {
public:
    GlyphOutlineCache() = default;

    /// Build a BLPath for the given glyph in `ft_face`.
    /// The path is positioned at (origin_x, origin_y).
    /// Returns an empty path if the glyph has no outline (e.g., bitmap font).
    /// Thread-safe: holds an internal lock during FT_Load_Glyph + decomposition.
    [[nodiscard]] BLPath build_outline(
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

    GlyphOutlineCache* outlines{nullptr};     // for stroke path building

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
        uint32_t fmt{BL_FORMAT_PRGB32};
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
    /// RAII handle to a checked-out scratch state.  Releases on destruction.\n    ///\n    /// Ownership invariant: ONE handle at a time owns a given TextScratchState.\n    /// Move construction / assignment explicitly null out the moved-from\n    /// (manager_, state_) pair so the moved-from destructor is a no-op\n    /// (no double-release back into the same manager).  This invariant is\n    /// what makes the manager TSan-clean under parallel draw_text_run()\n    /// invocations sharing a single TextScratchManager.\n    class Handle {
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

    /// Maximum number of TextScratchState objects retained between calls.\n    /// A 64-thread burst will amortize down to this many states, then\n    /// surplus releases delete the state outright.  Sized for typical\n    /// render pools without leaking per-thread scratch under sustained\n    /// multi-threaded draw_text_run() workloads.\n    static constexpr size_t kMaxPooledStates = 8;\n\n    Handle acquire();

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
// NOT in the render hot path.  Also owns the TextScratchManager that vends
// per-call scratch handles to draw_text_run() — engine-lifetime state but
// per-call allocation (FASE 3 thread-safety closure).

struct TextRenderResources {
    BLFontFaceCache bl_faces;

#ifdef CHRONON3D_ENABLE_TEXT
    FreeTypeFaceCache ft_faces;
    GlyphOutlineCache outlines;
#endif

    /// FASE 3 thread-safety: per-call scratch pool engine-owned.
    TextScratchManager scratch_manager;

    /// Build a FontFaceHandle for the given font path + size.
    /// Resolves the path via `resolver` before cache lookup.
    /// File opening happens inside the caches on first use — subsequent
    /// calls for the same (resolved_path, size) return cached data.
    [[nodiscard]] FontFaceHandle resolve_handle(
        const std::string& font_path,
        f32 font_size,
        const assets::AssetResolver& resolver
    );
};

} // namespace chronon3d

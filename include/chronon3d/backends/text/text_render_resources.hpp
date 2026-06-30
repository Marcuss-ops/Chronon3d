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

#include <mutex>
#include <string>
#include <unordered_map>

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
// FreeTypeFaceCache — thread-safe FT_Face loader
// ═══════════════════════════════════════════════════════════════════════════

#ifdef CHRONON3D_ENABLE_TEXT

class FreeTypeFaceCache {
public:
    FreeTypeFaceCache();
    ~FreeTypeFaceCache();

    FreeTypeFaceCache(const FreeTypeFaceCache&) = delete;
    FreeTypeFaceCache& operator=(const FreeTypeFaceCache&) = delete;

    /// Load (or return cached) FT_Face for the given resolved path + size.
    /// Returns nullptr if loading fails.
    [[nodiscard]] FT_Face get_face(const std::string& resolved_path, f32 font_size);

private:
    static FT_Library shared_library();

    FT_Face ft_face_{nullptr};
    std::string loaded_path_;
    f32 loaded_size_{0.0f};
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
    FT_Face           ft_face{nullptr};       // from FreeTypeFaceCache (null if not loaded)
#endif

    GlyphOutlineCache* outlines{nullptr};     // for stroke path building

    /// True if the BLFontFace is valid (the minimum requirement for fill).
    [[nodiscard]] bool valid() const noexcept {
        return bl_face != nullptr && !bl_face->empty();
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// TextRenderResources — aggregator owning all font caches
// ═══════════════════════════════════════════════════════════════════════════
//
// Owned by the renderer/backend.  `resolve_handle()` is the single entry
// point: it resolves the font path via the AssetResolver, looks up caches,
// and returns a FontFaceHandle.  File opening happens here (on first use),
// NOT in the render hot path.

struct TextRenderResources {
    BLFontFaceCache bl_faces;

#ifdef CHRONON3D_ENABLE_TEXT
    FreeTypeFaceCache ft_faces;
    GlyphOutlineCache outlines;
#endif

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

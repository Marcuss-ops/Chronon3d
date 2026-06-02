#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/core/types/types.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <cstddef>

namespace chronon3d {

// ── GlyphPosition ─────────────────────────────────────────────────────
// Precise placement of a single shaped glyph within a text run.
// Coordinates are in logical (unscaled) font units; multiply by
// (font_size / face_units_per_em) to get pixel values.

struct GlyphPosition {
    u32     glyph_id{0};         // font-specific glyph index
    float   x{0.0f};             // x offset in pixels (horizontal advance accumulated)
    float   y{0.0f};             // y offset in pixels (usually 0 for LTR, used for vertical)
    float   advance_x{0.0f};     // horizontal advance for this glyph (pixels)
    float   advance_y{0.0f};     // vertical advance (usually 0 for horizontal text)
    float   bbox_x0{0.0f};       // glyph ink bounding box (pixels)
    float   bbox_y0{0.0f};
    float   bbox_x1{0.0f};
    float   bbox_y1{0.0f};
    u32     cluster{0};          // byte index into original text for this glyph's cluster
    bool    is_cluster_start{false}; // first glyph of a cluster (e.g. ligature base)
};

// ── GlyphRun ──────────────────────────────────────────────────────────
// Result of shaping a text string. Provides pixel-accurate glyph
// positions, total width, and font vertical metrics.

struct GlyphRun {
    std::vector<GlyphPosition> glyphs;
    float width{0.0f};           // total advance width (pixels)
    float ascent{0.0f};          // from baseline to top (pixels, positive up)
    float descent{0.0f};         // from baseline to bottom (pixels, positive down)
    float baseline{0.0f};        // baseline y coordinate (pixels)
    float line_height{0.0f};     // recommended line height (pixels)
    float font_size{0.0f};       // the size this run was shaped at
};

// ── FontSpec ──────────────────────────────────────────────────────────
// Identifies a font face + style. The FontEngine resolves this to a
// loaded FreeType face + HarfBuzz font.

struct FontSpec {
    std::string font_path;       // absolute or asset-relative path to font file
    std::string font_family;     // family name (for fallback / diagnostics)
    int         font_weight{400}; // CSS-style weight: 100–900
    std::string font_style{"normal"}; // "normal", "italic", "oblique"

    bool operator==(const FontSpec& other) const noexcept {
        return font_path == other.font_path &&
               font_family == other.font_family &&
               font_weight == other.font_weight &&
               font_style == other.font_style;
    }
};

} // namespace chronon3d

namespace std {
template<> struct hash<chronon3d::FontSpec> {
    [[nodiscard]] size_t operator()(const chronon3d::FontSpec& s) const noexcept {
        size_t h = 0;
        h ^= std::hash<std::string>{}(s.font_path) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::string>{}(s.font_family) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(s.font_weight) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::string>{}(s.font_style) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
} // namespace std

namespace chronon3d {

// ── FontEngine ────────────────────────────────────────────────────────
// Thread-safe font loading and text shaping using FreeType + HarfBuzz.
//
// Usage:
//   FontEngine engine;
//   auto run = engine.shape_text("Hello", FontSpec{"path.ttf", "Family", 400}, 32.0f);
//   for (const auto& g : run.glyphs) { /* ... */ }

class FontEngine {
public:
    FontEngine();
    ~FontEngine();

    // Non-copyable (holds FT_Library handle)
    FontEngine(const FontEngine&) = delete;
    FontEngine& operator=(const FontEngine&) = delete;
    FontEngine(FontEngine&&) noexcept;
    FontEngine& operator=(FontEngine&&) noexcept;

    /// Shape a string of text into a GlyphRun at the given font size.
    /// Returns std::nullopt if the font cannot be loaded.
    [[nodiscard]] std::optional<GlyphRun> shape_text(
        std::string_view text,
        const FontSpec& spec,
        float font_size
    ) const;

    /// Measure a single line of text (no wrapping). Returns total width
    /// in pixels, or 0.0f if the font cannot be loaded.
    [[nodiscard]] float measure_text(
        std::string_view text,
        const FontSpec& spec,
        float font_size
    ) const;

    /// Return font metrics (ascent, descent, line_height) in pixels for
    /// the given font spec and size. Returns zeros if font not found.
    struct FontMetrics {
        float ascent{0.0f};
        float descent{0.0f};
        float line_height{0.0f};
        float x_height{0.0f};
        float cap_height{0.0f};
        float max_advance{0.0f};
    };
    [[nodiscard]] FontMetrics get_font_metrics(
        const FontSpec& spec,
        float font_size
    ) const;

    /// Clear the internal face cache (useful after font file changes).
    void clear_cache();

    /// Return true if the given font file can be loaded.
    [[nodiscard]] bool can_load(const FontSpec& spec);

    /// Return the number of cached glyph bounding-box entries
    /// (diagnostic / testing helper).
    [[nodiscard]] size_t glyph_bbox_cache_size() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// ── Free functions ────────────────────────────────────────────────────

/// Shape text using a default global FontEngine instance.
/// Convenience for one-off shaping without managing an engine.
[[nodiscard]] std::optional<GlyphRun> shape_text(
    std::string_view text,
    const FontSpec& spec,
    float font_size
);

/// Return a process-wide shared FontEngine singleton.
/// Useful for pipeline stages that do not have access to a LayerBuilder
/// but still want to amortise face-loading costs.
[[nodiscard]] FontEngine& shared_font_engine();

/// Reset the process-wide shared FontEngine singleton.
/// Clears all cached font faces, glyph bounding boxes, and HarfBuzz
/// font objects. Useful for font hot-reload: call this after updating
/// font files on disk, then the next call to shared_font_engine() will
/// lazily reload faces.
void reset_shared_font_engine();

} // namespace chronon3d

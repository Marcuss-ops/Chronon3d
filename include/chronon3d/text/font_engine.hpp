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
    float   x{0.0f};             // cumulative x position (cursor + hb offset) in pixels
    float   y{0.0f};             // cumulative y position in pixels
    float   x_offset{0.0f};      // raw HarfBuzz x_offset (relative, not cumulative)
    float   y_offset{0.0f};      // raw HarfBuzz y_offset (relative, not cumulative)
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
    f32         font_size{72.0f}; // target pixel size (carried for TextSpec composition)

    bool operator==(const FontSpec& other) const noexcept {
        return font_path == other.font_path &&
               font_family == other.font_family &&
               font_weight == other.font_weight &&
               font_style == other.font_style;
    }
};

/// Shaping direction for non-Latin / complex-script text.
/// When Auto, the shaping engine detects RTL from the first
/// strongly-directional character (Arabic, Hebrew, etc.).
enum class TextDirection {
    Auto,
    LTR,
    RTL,
};

/// Wrapping mode for text layout.
///   None      — text extends beyond box bounds (no wrapping).
///   Word      — break at word boundaries (spaces, hyphens).
///   Character — break at any character (including mid-word).
enum class TextWrap {
    None,
    Word,
    Character,
};

// ── TextShaping ───────────────────────────────────────────────────────
// Optional per-call shaping parameters.  Forwarded to HarfBuzz so we get
// correct shaping for non-Latin scripts (Arabic, Hebrew, Devanagari, CJK,
// etc.) and to select a BCP-47 language tag for hyphenation / OpenType
// language-specific features.
//
// Defaults match the historical Latin-only behaviour for full
// backward-compatibility with existing call sites.
struct TextShaping {
    // Direction: Auto (auto-detect), LTR, or RTL.
    TextDirection direction{TextDirection::Auto};

    // HarfBuzz script tag (HB_SCRIPT_*).  The 4-byte OpenType script tag
    // is passed through to hb_buffer_set_script() when non-zero; when 0
    // (default) the implementation leaves it as HB_SCRIPT_INVALID so
    // hb_buffer_guess_segment_properties() auto-detects the script from
    // the text content (Latin, Arabic, Devanagari, CJK, etc.).
    //
    // Common values (for explicit opt-in):
    //   HB_SCRIPT_COMMON   = 0x5A797979
    //   HB_SCRIPT_LATIN    = 0x4C61746E
    //   HB_SCRIPT_ARABIC   = 0x41726162
    //   HB_SCRIPT_HEBREW   = 0x48656272
    //   HB_SCRIPT_DEVANAGARI = 0x44657661
    //   HB_SCRIPT_HAN      = 0x48616E20
    // Default: 0 → auto-detect via guess_segment_properties.
    int  script{0};

    // BCP-47 language tag, e.g. "en", "ar", "he", "hi", "zh-Hans".
    // When empty (default), hb_buffer_guess_segment_properties()
    // auto-detects the language from the text's character range.
    std::string language{};
};

// ── PlacedGlyph ─────────────────────────────────────────────────────────
// A single glyph with resolved, tracking-aware positioning.
// Produced by resolve_placed_glyph_run() from a HarfBuzz-shaped GlyphRun.
// Contains:
//   - Raw HarfBuzz offset & advance
//   - Resolved cumulative x,y position (pen_accumulated + x_offset)
//   - Advance with per-cluster tracking baked in
//   - Source-text byte range and cluster info

struct PlacedGlyph {
    u32     glyph_id{0};
    float   advance_x{0.0f};       // advance INCLUDING per-cluster tracking
    float   raw_advance_x{0.0f};   // advance WITHOUT tracking (raw HarfBuzz advance)
    float   advance_y{0.0f};
    float   x_offset{0.0f};        // raw HarfBuzz relative offset
    float   y_offset{0.0f};
    float   x{0.0f};               // resolved cumulative x position = pen_x + x_offset
    float   y{0.0f};               // resolved cumulative y position = pen_y + y_offset
    size_t  byte_offset{0};        // byte offset in source text for this glyph's cluster
    size_t  byte_len{0};           // bytes in source text for this glyph's cluster
    u32     cluster{0};            // HarfBuzz cluster value
    bool    is_cluster_start{false}; // first glyph of a HarfBuzz cluster
};

// ── PlacedGlyphRun ─────────────────────────────────────────────────────
// Fully-resolved glyph run with tracking applied and cluster information.
// Every consumer (fill, stroke, typewriter, text animator) should use this
// as the single source of truth for glyph positioning, eliminating the
// duplicated tracking-logic and cluster-map code that previously existed
// in HbToBlGlyphRun, FtGlyphPathBuilder, compute_typewriter_layout, and
// TextAnimator::split_glyphs.

struct PlacedGlyphRun {
    std::vector<PlacedGlyph> glyphs;
    float total_width{0.0f};       // total advance width including tracking
    float total_height{0.0f};      // total height (ascent + descent)
    float ascent{0.0f};
    float descent{0.0f};
    float baseline{0.0f};
    float font_size{0.0f};

    /// Information about a single HarfBuzz-level cluster.
    /// Multiple clusters may merge to form one grapheme cluster.
    struct Cluster {
        size_t start_glyph{0};     // index into glyphs
        size_t end_glyph{0};       // exclusive
        size_t byte_offset{0};     // byte offset in source text
        size_t byte_len{0};        // bytes in source text
        float  advance{0.0f};      // total advance for this cluster (including tracking)
        float  raw_advance{0.0f};  // total advance WITHOUT tracking
    };
    std::vector<Cluster> clusters;
};

/// Resolve a HarfBuzz GlyphRun into placed glyph positions with tracking.
///
/// @param hb_run       The HarfBuzz shaping result
/// @param tracking     Extra per-cluster spacing in pixels
/// @param source_text  Optional source text; if provided, cluster byte-ranges
///                     are populated (otherwise byte_offset/byte_len are 0)
/// @return A PlacedGlyphRun with resolved positions and cluster info
[[nodiscard]] PlacedGlyphRun resolve_placed_glyph_run(
    const GlyphRun& hb_run,
    float tracking = 0.0f,
    std::string_view source_text = {}
);

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
    /// Optional `shaping` selects the script and language forwarded to
    /// HarfBuzz — leave it default for Latin text.
    /// Returns std::nullopt if the font cannot be loaded.
    [[nodiscard]] std::optional<GlyphRun> shape_text(
        std::string_view text,
        const FontSpec& spec,
        float font_size,
        const TextShaping& shaping = TextShaping{}
    ) const;

    /// Measure a single line of text (no wrapping). Returns total width
    /// in pixels, or 0.0f if the font cannot be loaded.
    [[nodiscard]] float measure_text(
        std::string_view text,
        const FontSpec& spec,
        float font_size,
        const TextShaping& shaping = TextShaping{}
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
    float font_size,
    const TextShaping& shaping = TextShaping{}
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

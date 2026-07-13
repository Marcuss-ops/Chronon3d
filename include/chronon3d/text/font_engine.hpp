#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/text/text_direction.hpp>

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <cstddef>

// ── WP-8 PR 8.0 — typed asset resolver plumbing ────────────────────────
//
// `font_engine.hpp` is a public SDK header consumed by tests, content
// helpers, scene builders, and the CLI toolchain.  We forward-declare
// the resolver to keep this header lightweight (matching the identical
// pattern in `render_graph_context.hpp`).  The full definition lives in
// `<chronon3d/assets/asset_resolver.hpp>`.  Callers that dereference
// the resolver pointer MUST include that header themselves.
namespace chronon3d::assets { class AssetResolver; }

namespace chronon3d::content::text { class TypewriterLayoutCache; }

namespace chronon3d {
// ── GlyphPosition
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

// ═══════════════════════════════════════════════════════════════════════════
// FontIdentity — the FONT_IDENTITY subset of FontSpec.
// ═══════════════════════════════════════════════════════════════════════════
//
// Excludes font_size because size is a LAYOUT concern, not a font identity:
//   * superscript / subscript are the same font at different sizes;
//   * drop-cap scaling keeps one font across size variation;
//   * emphasis gradients (e.g. progressive typewriter intro) re-use one
//     font across a range of sizes.
//
// Two paragraphs sharing the same FontIdentity at different sizes are
// rendering the SAME font.  This is the same semantic contract that
// `paragraph_is_multi_font` enforces via its std::tie compare (see
// include/chronon3d/text/text_resolver.hpp — also kept in sync with
// FontSpec::operator== above; any new identity field added to FontSpec
// MUST be added here too).
//
// Used by `ShapedFontSpan` (text_run.hpp) to label which glyph ranges of a
// TextRunLayout share a font.  The runtime renderer iterates the spans
// and switches BLFont at boundaries.  Compile-time, font_identity_of()
// is the canonical projection from a full FontSpec to its identity.
struct FontIdentity {
    std::string font_path;
    std::string font_family;
    int         font_weight{400};
    std::string font_style{"normal"};

    bool operator==(const FontIdentity& o) const noexcept {
        return font_path == o.font_path &&
               font_family == o.font_family &&
               font_weight == o.font_weight &&
               font_style == o.font_style;
    }
    bool operator!=(const FontIdentity& o) const noexcept { return !(*this == o); }
};

/// Drop-in projection from a full FontSpec to its FontIdentity.
/// font_size is intentionally DROPPED (see FontIdentity header doc).
[[nodiscard]] inline FontIdentity font_identity_of(const FontSpec& s) noexcept {
    return FontIdentity{s.font_path, s.font_family, s.font_weight, s.font_style};
}

// ═══════════════════════════════════════════════════════════════════════════
// FontLayoutIdentity — canonical font identity FOR LAYOUT purposes.
// ═══════════════════════════════════════════════════════════════════════════
//
// Unlike FontIdentity (which excludes font_size because size is a layout
// concern), FontLayoutIdentity INCLUDES size, shaping features, AND
// variable-font variation_axes because these are part of the layout's identity:
//   * Two text runs at 16pt vs 72pt produce different glyph placements;
//   * Different OpenType feature strings (kern=1 vs kern=0) produce
//     different advances on ligature-heavy runs.
//   * Variable font axes (wght=400 vs wght=700) produce different glyph
//     outlines and metrics.
//
// Used by ALL locations that hash or compare font identity for layout
// purposes:
//   1. TextRunLayout::layout_hash()
//   2. TextRunLayout::shaping_hash()
//   3. TextLayoutCacheKey::digest()
//   4. build_cache_key()
//   5. apply_active_state_to_text_run_shape() fast-path
//   6. prewarm_text_run_layout_for_frame()
//
// M1.5#4 — canonical field names: resolved_path, family, weight, style,
// size, features, variation_axes (prefixed font_ names removed).
struct FontLayoutIdentity {
    std::string resolved_path;   // resolved font file path (may be empty for family-only fallback)
    std::string family;          // CSS family name (case-insensitive, canonicalized)
    int         weight{400};
    std::string style{"normal"};
    float       size{32.0f};
    std::string features;        // OpenType shaping features (e.g. "kern=1,liga=1")
    std::string variation_axes;  // OpenType variable font axes (e.g. "wght=700,wdth=100")

    [[nodiscard]] bool operator==(const FontLayoutIdentity& o) const noexcept {
        return resolved_path == o.resolved_path &&
               family == o.family &&
               weight == o.weight &&
               style == o.style &&
               size == o.size &&
               features == o.features &&
               variation_axes == o.variation_axes;
    }
    [[nodiscard]] bool operator!=(const FontLayoutIdentity& o) const noexcept {
        return !(*this == o);
    }
};

/// Project a FontSpec + size + features + variation_axes into a FontLayoutIdentity.
/// Canonical entry point for extracting the layout-relevant font identity
/// from the scattered (font_path, font_family, weight, style, size, features,
/// variation_axes) tuple used by cache keys, layout hashes, and fast-path comparisons.
[[nodiscard]] inline FontLayoutIdentity font_layout_identity_of(
    const FontSpec& font,
    float sz,
    const std::string& feat,
    const std::string& var_axes = {}
) noexcept {
    return FontLayoutIdentity{
        font.font_path, font.font_family,
        font.font_weight, font.font_style,
        sz, feat, var_axes
    };
}

/// Overload that derives size + features + variation_axes from a TextRunLayout reference.
/// Kept as a forward-declared free function because it depends on
/// TextRunLayout (defined in text_run.hpp, which includes this header).
/// The implementation lives in text_run.cpp.
struct TextRunLayout;
[[nodiscard]] FontLayoutIdentity font_layout_identity_of(const TextRunLayout& layout) noexcept;

/// Shaping direction for non-Latin / complex-script text.
/// When Auto, the shaping engine detects RTL from the first
/// strongly-directional character (Arabic, Hebrew, etc.).
/// Defined in text_direction.hpp.

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
    /// WP-8 PR 8.0 — explicit AssetResolver ctor.  Every FontEngine owns a
    /// non-owning pointer to the runtime-owned resolver; `load_face` and
    /// the public shaping methods use it for every relative font-path
    /// resolution.  This is the canonical ctor; the default ctor below is
    /// a thin transitional wrapper kept for PR 8.0 only and scheduled
    /// for deletion in PR 8.1.
    explicit FontEngine(const chronon3d::assets::AssetResolver& resolver);

    FontEngine() = delete;

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

    /// Per-runtime cache for typewriter text layouts.
    [[nodiscard]] chronon3d::content::text::TypewriterLayoutCache& typewriter_layout_cache();

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

/// WP-8 PR 8.0 — global shaping convenience, reimplemented on top of a
/// lazy process-local `static FontEngine` constructed once against
/// `runtime::typed_resolver_for_deep_code`.  The legacy
/// `shared_font_engine()` singleton has been REMOVED in PR 8.0 — the
/// shared-font-engine accessor violates the per-engine asset-isolation
/// contract in PR 8.2.  Production code should construct a
/// `FontEngine{runtime.resolver()}` (or pass the resolver through) and
/// keep it on the owning context.  This free function remains for the
/// leg-up test convenience and for one-off shaping inside diagnostics.
[[nodiscard]] std::optional<GlyphRun> shape_text(
    std::string_view text,
    const FontSpec& spec,
    float font_size,
    const TextShaping& shaping = TextShaping{}
);

// NOTE: WP-8 PR 8.0 REMOVED from this header:
//   [[nodiscard]] FontEngine& shared_font_engine();
//   void reset_shared_font_engine();
// Both were subscribed to `runtime::typed_resolver_for_deep_code()`
// internally and depended on a process-wide singleton engine that PR 8.1
// will not allow.  Production callers must keep their own FontEngine
// instance bound to an explicit `const AssetResolver&`.

} // namespace chronon3d

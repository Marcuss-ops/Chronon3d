#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// composer_types.hpp — Internal data model for the paragraph composer
//
// These types sit between the HarfBuzz-shaped PlacedGlyphRun and the final
// TextRunLayout.  They capture the breakable units (ShapedCluster), the
// composed lines with justification adjustments (ComposedLine), and the
// complete paragraph layout (ParagraphLayout).
//
// PR 2 provides the SingleLine composer; EveryLine follows in PR 3.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <cstddef>
#include <vector>

namespace chronon3d {

// ── ShapedCluster — a breakable unit from HarfBuzz shaping ───────────────
//
// Each ShapedCluster wraps a HarfBuzz cluster (one or more glyphs that
// represent a single typographic unit: a grapheme, a ligature, or an
// Arabic contextual form).  The composer MUST NOT split a cluster across
// lines — that would break ligatures and contextual shaping.
//
// Fields are derived from PlacedGlyphRun::Cluster + the source text.

struct ShapedCluster {
    /// Byte range in the original UTF-8 text.
    size_t source_byte_start{0};
    size_t source_byte_end{0};

    /// Glyph range in PlacedGlyphRun::glyphs.
    size_t first_glyph{0};
    size_t glyph_count{0};

    /// Total advance width of this cluster (pixels, includes tracking).
    float advance{0.0f};

    /// Font vertical metrics for this cluster (ascent/descent in pixels).
    float ascent{0.0f};
    float descent{0.0f};

    // ── Break classification ────────────────────────────────────────────

    /// True if this cluster is whitespace (space, tab, non-breaking space, etc.).
    bool whitespace{false};

    /// True if this cluster is punctuation that may overhang the margin.
    bool punctuation{false};

    /// True if this cluster is a mandatory line break (\\n, \\r, U+2029).
    /// The cluster itself is emitted, then a new line starts after it.
    bool mandatory_break{false};

    /// True if the composer is allowed to break the line AFTER this cluster.
    /// Whitespace clusters and certain punctuation allow break-after.
    bool allowed_break_after{false};

    /// True if this cluster contains a hyphenation point (soft hyphen U+00AD
    /// or a hyphenation dictionary match).  PR 2 ignores this; PR 3+
    /// (hyphenation) uses it.
    bool hyphenation_point{false};
};

// ── ComposedLine — a single line after breaking and justification ────────

struct ComposedLine {
    /// Index range into the ShapedCluster vector.
    size_t first_cluster{0};
    size_t cluster_count{0};

    /// Natural advance width (sum of cluster advances, before justification).
    float natural_width{0.0f};

    /// Final width after justification adjustments (should <= available_width).
    float final_width{0.0f};

    /// Y position of the baseline (relative to the paragraph origin).
    float baseline_y{0.0f};

    /// Justification adjustments for this line.
    /// These are applied to each cluster during final positioning.
    float word_spacing_adjustment{0.0f};
    float letter_spacing_adjustment{0.0f};
    float glyph_scale{1.0f};

    /// Hanging punctuation optical overhang (pixels).
    /// Positive = extends beyond margin.
    float left_overhang{0.0f};
    float right_overhang{0.0f};
};

// ── ParagraphLayout — complete layout for one paragraph ──────────────────

struct ParagraphLayout {
    /// Composed lines, top to bottom.
    std::vector<ComposedLine> lines;

    /// Total bounding box of the paragraph (width, height).
    /// Includes left/right indent and hanging punctuation overhang.
    Vec2 bounds{0.0f, 0.0f};
};

} // namespace chronon3d

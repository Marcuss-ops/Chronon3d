#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_path_spec.hpp — Text-on-path style descriptor
//
// PR 5 of the text pipeline.  Specifies how text is placed along a Bézier
// path: which path, alignment, overflow behaviour, margins, and whether
// glyphs rotate perpendicular to the path tangent.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/path.hpp>

#include <memory>

namespace chronon3d {

// ── TextPathAlignment — where text starts relative to available length ──

enum class TextPathAlignment : u8 {
    /// Text starts at first_margin.
    Start,

    /// Text is centred in the available space.
    Center,

    /// Text ends at total_length - last_margin.
    End,

    /// Text is distributed across the full available length via
    /// extra tracking between clusters.
    Justify,
};

// ── TextPathOverflow — what happens when text is longer than the path ──

enum class TextPathOverflow : u8 {
    /// Clusters beyond the usable length are dropped.
    Clip,

    /// Clusters continue past the end of the path (path tangent is
    /// extrapolated from the last sample).
    Continue,

    /// On a closed path, overflow wraps around to the start.
    WrapClosedPath,
};

// ── TextPathSpec ────────────────────────────────────────────────────────

struct TextPathSpec {
    /// The path to place text along.  Must be non-null for text-on-path.
    std::shared_ptr<const PathShape> path;

    /// Distance from the path start before text begins (pixels).
    float first_margin{0.0f};

    /// Distance from the path end after text ends (pixels).
    float last_margin{0.0f};

    /// Offset perpendicular to the path (pixels).
    /// Positive = along the normal direction (downward in screen coords),
    /// negative = opposite the normal (upward).
    float baseline_offset{0.0f};

    /// Horizontal alignment of text within the available space.
    TextPathAlignment alignment{TextPathAlignment::Start};

    /// Overflow behaviour when text exceeds usable path length.
    TextPathOverflow overflow{TextPathOverflow::Clip};

    /// If true, the path direction is reversed (text flows opposite).
    bool reverse_path{false};

    /// If true, each cluster is rotated so its baseline follows the path
    /// tangent.  If false, clusters keep their default upright orientation.
    bool perpendicular_to_path{true};

    /// If true and alignment == Justify, tracking is stretched to fill the
    /// full available length even if the text would otherwise fit naturally.
    bool force_alignment{false};

    /// If true, the normal is flipped (useful for text below a path).
    bool flip_normal{false};

    /// If true, the path is treated as closed (end connects to start).
    /// When combined with WrapClosedPath overflow, clusters past the end
    /// wrap around to the path start.
    bool closed{false};
};

} // namespace chronon3d

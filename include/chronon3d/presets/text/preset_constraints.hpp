#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// preset_constraints.hpp — Responsive preset sizing helpers
//
// Shared types used by the Chronon3D text preset families.  Keeping them
// in one header avoids duplication between text_presets.hpp and
// text_presets_v1.hpp (AGENTS.md §anti-duplication).
//
//   TextBoxConstraints  — fractional + minimum box sizing
//   AspectRatioPolicy   — how to resolve constraints on non-16:9 canvases
//   resolve_text_box_constraints() — canonical resolution function
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/math/glm_types.hpp>             // Vec2
#include <chronon3d/text/resolve_text_placement.hpp>  // CanvasInfo

#include <algorithm>

namespace chronon3d::presets::text {

/// Describes how a preset text box should be sized relative to the canvas.
/// Width and height are expressed as fractions of the canvas dimensions.
/// Minimum absolute sizes prevent the box from collapsing on tiny canvases.
struct TextBoxConstraints {
    /// Fraction of canvas width used for the box width (0..1).
    float width_fraction{1.0f};
    /// Fraction of canvas height used for the box height (0..1).
    float height_fraction{1.0f};
    /// Minimum absolute width (pixels).
    float min_width{0.0f};
    /// Minimum absolute height (pixels).
    float min_height{0.0f};
};

/// Policy that controls how `TextBoxConstraints` are resolved when the
/// target canvas has a different aspect ratio than the canonical 16:9.
enum class AspectRatioPolicy {
    /// Scale width and height independently against the canvas dimensions.
    /// Best for text boxes that must fill the available horizontal/vertical
    /// space (default for all presets).
    FitCanvas,
    /// Uniformly scale the box so it fits entirely inside the canvas,
    /// preserving the canonical 16:9 box aspect ratio.
    FitInside,
    /// Uniformly scale the box so it covers the canvas, preserving the
    /// canonical 16:9 box aspect ratio (may overflow one axis).
    FitCover,
};

/// Resolve constraints into an absolute pixel box size for the given canvas.
///
/// For `FitCanvas` the width and height are scaled independently against
/// the canvas dimensions.  For `FitInside`/`FitCover` the box is treated as
/// if it were designed for a 1920×1080 reference canvas and then scaled
/// uniformly so the result fits inside or covers the target canvas.
[[nodiscard]] inline Vec2 resolve_text_box_constraints(
    const TextBoxConstraints& constraints,
    const CanvasInfo& canvas,
    AspectRatioPolicy policy = AspectRatioPolicy::FitCanvas) noexcept
{
    Vec2 size{
        canvas.width  * constraints.width_fraction,
        canvas.height * constraints.height_fraction,
    };

    if (policy == AspectRatioPolicy::FitInside || policy == AspectRatioPolicy::FitCover) {
        // Reference box on the canonical 1920×1080 canvas.
        const Vec2 reference_size{
            1920.0f * constraints.width_fraction,
            1080.0f * constraints.height_fraction,
        };

        // Scale factor to fit/cover the canvas while preserving the
        // reference box aspect ratio.
        const float scale = (policy == AspectRatioPolicy::FitInside)
            ? std::min(canvas.width / reference_size.x, canvas.height / reference_size.y)
            : std::max(canvas.width / reference_size.x, canvas.height / reference_size.y);

        size.x = scale * reference_size.x;
        size.y = scale * reference_size.y;
    }

    size.x = std::max(size.x, constraints.min_width);
    size.y = std::max(size.y, constraints.min_height);
    return size;
}

} // namespace chronon3d::presets::text

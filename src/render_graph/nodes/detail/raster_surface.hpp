#pragma once

// Internal projected-surface geometry contract.
//
// This type deliberately owns geometry only. The framebuffer lifetime remains
// owned by the render graph / framebuffer pool, so future image and SVG
// producers can reuse the same origin + content-size contract without adding
// another surface manager or cache.

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/text/text_run_geometry.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace chronon3d::graph::detail {

struct RasterSurfaceGeometry {
    /// Local-space coordinate represented by the first pixel of the surface.
    Vec2 origin{0.0f, 0.0f};
    /// Logical local-space extent represented by the surface.
    Vec2 content_size{0.0f, 0.0f};
    /// Bounds before the raster safety margin is applied.
    Vec2 ink_min{0.0f, 0.0f};
    Vec2 ink_max{0.0f, 0.0f};

    [[nodiscard]] bool valid() const noexcept {
        return content_size.x > 0.0f && content_size.y > 0.0f &&
               std::isfinite(origin.x) && std::isfinite(origin.y) &&
               std::isfinite(content_size.x) && std::isfinite(content_size.y);
    }

    [[nodiscard]] i32 width() const noexcept {
        return std::max<i32>(1, static_cast<i32>(std::ceil(content_size.x)));
    }

    [[nodiscard]] i32 height() const noexcept {
        return std::max<i32>(1, static_cast<i32>(std::ceil(content_size.y)));
    }
};

/// Compute the canonical tight surface for a TextRun.
///
/// The bounds are shared by graph projection and software raster preparation:
/// glyph ink, animated shear/scale, stroke/blur, shadows, and the same safety
/// margin are all included before integer dimensions are chosen.
[[nodiscard]] inline std::optional<RasterSurfaceGeometry>
compute_tight_text_surface_geometry(
    const TextRunShape& shape,
    f32 margin = 8.0f)
{
    if (!shape.layout || shape.glyphs.empty()) {
        return std::nullopt;
    }

    const auto local = renderer::compute_text_run_visual_bounds(shape);
    if (!local) {
        return std::nullopt;
    }

    f32 min_x = local->min_x;
    f32 min_y = local->min_y;
    f32 max_x = local->max_x;
    f32 max_y = local->max_y;

    // Keep shadow expansion identical to prepare_text_run(). Shadows are
    // paint bounds, not glyph-layout bounds, and therefore belong in the
    // producer-side surface contract.
    const float font_size = shape.layout->font_size;
    const float ascent = std::max({0.0f, shape.layout->placed.ascent,
                                   font_size * 0.8f});
    const float descent = std::max({0.0f, shape.layout->placed.descent,
                                    font_size * 0.2f});
    const auto& placed = shape.layout->placed;
    const std::size_t shadow_glyph_count = std::min(
        shape.glyphs.size(), placed.glyphs.size());
    for (std::size_t gi = 0; gi < shadow_glyph_count; ++gi) {
        const auto& glyph = shape.glyphs[gi];
        const float gx = glyph.layout_position.x + glyph.position.x;
        const float gy = glyph.layout_position.y + glyph.position.y;
        const float scale_y = std::abs(glyph.scale.y * glyph.scale.z);
        const float advance = std::max(1.0f, std::abs(placed.glyphs[gi].advance_x));
        for (const auto& shadow : shape.shadows) {
            if (!shadow.enabled) continue;
            const float padding = shadow.blur + std::abs(shadow.offset.x) +
                                  std::abs(shadow.offset.y) + margin;
            min_x = std::min(min_x, gx - padding);
            max_x = std::max(max_x, gx + advance + padding);
            min_y = std::min(min_y, gy - ascent * scale_y - padding);
            max_y = std::max(max_y, gy + descent * scale_y + padding);
        }
    }

    if (!(max_x > min_x && max_y > min_y)) {
        return std::nullopt;
    }

    RasterSurfaceGeometry result;
    result.ink_min = {min_x, min_y};
    result.ink_max = {max_x, max_y};
    result.origin = {min_x - margin, min_y - margin};
    result.content_size = {
        std::max(1.0f, max_x - min_x + margin * 2.0f),
        std::max(1.0f, max_y - min_y + margin * 2.0f)
    };
    return result;
}

} // namespace chronon3d::graph::detail

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

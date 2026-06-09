#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>

namespace chronon3d {
struct RenderState;
}

namespace chronon3d::renderer {

/// Renders a grid background into @p fb using a sparse column/row precomputation.
/// The algorithm computes per-column weights once, then for each row checks
/// whether a horizontal grid line is present. If not, only the active columns
/// are touched (rows with no grid line skip the full horizontal scan).
///
/// The visual result is identical to the pixel-by-pixel loop in the old
/// SoftwareGridBackgroundProcessor, but with far fewer calls to cell_distance()
/// and line_weight().
void render_grid_background_kernel(
    Framebuffer& fb,
    const GridBackgroundShape& grid,
    const raster::BBox& clip,
    const RenderState& state);

} // namespace chronon3d::renderer

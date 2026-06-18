#pragma once

// ---------------------------------------------------------------------------
// text_run_geometry.hpp — Backend-independent text run geometry utilities
//
// Extracted from backends/software/text_run_processor.hpp so that the render
// graph (TextRunNode, MultiSourceNode) can compute bounding boxes without
// pulling in the software backend.
// ---------------------------------------------------------------------------

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/math/raster_utils.hpp>

namespace chronon3d::renderer {

/// Compute the world-space bounding box of a text run.
///
/// Uses glyph positions (layout + animated offset), per-glyph 2.5D shear
/// estimates (rotation.x/y), scale.z expansion, blur, stroke width, and a
/// caller-supplied spread (shadow/glow padding) to produce a conservative
/// box that is then transformed by @p model into world space.
///
/// This function is pure geometry — it has no dependency on any rendering
/// backend and can be used from the render graph, preflight, or tests.
[[nodiscard]] raster::BBox compute_text_run_world_bbox(
    const TextRunShape& shape,
    const Mat4& model,
    f32 spread
);

} // namespace chronon3d::renderer

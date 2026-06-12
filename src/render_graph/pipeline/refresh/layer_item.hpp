#pragma once

// ---------------------------------------------------------------------------
// refresh/layer_item.hpp
//
// Shared helper: build a LayerGraphItem from a resolved layer + context.
// Used by source, multi_source and transform refreshers.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include "../../builder/graph_builder_internal.hpp"
#include "../../builder/graph_builder_pipeline.hpp"
#include "../../builder/graph_builder_bbox.hpp"

namespace chronon3d::graph::detail {

/// Build a LayerGraphItem for a resolved layer at the current frame.
/// Handles 2.5D projection when the camera is active and the layer
/// uses 3D projection, otherwise returns a passive (non-projected) item.
[[nodiscard]] LayerGraphItem make_layer_graph_item_for_refresh(
    const ResolvedLayer& resolved_layer,
    const RenderGraphContext& ctx);

} // namespace chronon3d::graph::detail

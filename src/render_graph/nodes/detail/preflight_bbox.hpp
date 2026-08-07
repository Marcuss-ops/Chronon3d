#pragma once

#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <optional>

namespace chronon3d::graph {

class SourceNode;
class MultiSourceNode;

namespace detail {

[[nodiscard]] std::optional<raster::BBox> preflight_diagnostic_bbox(
    const SourceNode& node,
    const RenderGraphContext& ctx);

[[nodiscard]] std::optional<raster::BBox> preflight_diagnostic_bbox(
    const MultiSourceNode& node,
    const RenderGraphContext& ctx);

} // namespace detail
} // namespace chronon3d::graph

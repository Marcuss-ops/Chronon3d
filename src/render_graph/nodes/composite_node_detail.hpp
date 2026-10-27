#pragma once

#include <chronon3d/render_graph/nodes/composite_node.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <optional>
#include <string_view>

namespace chronon3d::graph::detail {

bool try_native_affine_composite(
    RenderGraphContext&, Framebuffer&, const Framebuffer&,
    const std::optional<raster::BBox>&);
bool try_native_composite(
    RenderGraphContext&, Framebuffer&, Framebuffer&,
    BlendMode, CompositeOperator, const std::optional<raster::BBox>&);
bool seed_native_destination(
    RenderGraphContext&, Framebuffer&, const Framebuffer&);
[[nodiscard]] std::optional<NodeExecutionError> materialize_native_or_fail(
    RenderGraphContext&, Framebuffer&, const Framebuffer&, std::string_view);

} // namespace chronon3d::graph::detail

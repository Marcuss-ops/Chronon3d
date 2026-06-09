#pragma once

#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include "graph_builder_internal.hpp"
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <memory_resource>

namespace chronon3d {
    class SoftwareRenderer;
}

namespace chronon3d::graph {
    struct ShadowCasterInfo;
}

namespace chronon3d::rendering {
    struct DepthGrade;
}

namespace chronon3d::graph::detail {

// ── Core builder functions ────────────────────────────────────────────

bool is_native_3d_layer(const Layer& layer);
raster::BBox compute_layer_bbox(const LayerGraphItem& item, const RenderGraphContext& ctx, SoftwareRenderer* renderer);

// ── Helper functions (Phase 4: exposed for pass classes) ──────────────

GraphNodeId append_root_sources(RenderGraph& graph, const Scene& scene,
                                RenderGraphContext& ctx, GraphNodeId current);

GraphNodeId build_matte_sub_pipeline(RenderGraph& graph, const LayerGraphItem& item,
                                      const RenderGraphContext& ctx);

void append_layer_pipeline(RenderGraph& graph, const LayerGraphItem& item,
                           GraphNodeId& current, RenderGraphContext& ctx,
                           const Camera2_5DRuntime& cam25d,
                           std::span<const ShadowCasterInfo> casters,
                           const rendering::DepthGrade& depth_grade);

void sort_camera25d_layers(std::vector<LayerGraphItem>& items);

LayerGraphItem make_item_for_matte_source(const ResolvedLayer& rl,
                                           const RenderGraphContext& ctx,
                                           const Camera2_5DRuntime& cam25d,
                                           const std::unordered_map<std::string, bool>& is_static_cache);

void compute_static_layers(const std::pmr::vector<ResolvedLayer>& layers,
                           std::unordered_map<std::string, bool>& is_static_cache);

void compute_static_layers(const LayerResolutionResult& resolved,
                           std::unordered_map<std::string, bool>& is_static_cache);

bool is_full_frame_opaque(GraphNodeId id, const RenderGraph& graph,
                          const RenderGraphContext& ctx,
                          std::unordered_map<GraphNodeId, bool>& memo);

} // namespace chronon3d::graph::detail

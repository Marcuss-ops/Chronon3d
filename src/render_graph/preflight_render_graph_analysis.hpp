#pragma once

// preflight_render_graph_analysis.hpp
//
// Internal header declaring analysis phases extracted from
// debug_preflight_render_graph().  Included by preflight_render_graph.cpp
// and implemented by preflight_render_graph_analysis.cpp.
//
// NOT part of the public API — do not include outside preflight_render_graph*.cpp.

#include <chronon3d/render_graph/preflight_render_graph.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <cstdint>
#include <vector>

namespace chronon3d::graph {

void populate_node_basics(
    const RenderGraph& graph,
    const RenderGraphContext& ctx,
    const std::vector<int>& output_degree,
    const raster::BBox& canvas,
    int64_t canvas_area,
    GraphPreflightReport& report);

std::vector<GraphNodeId> topo_sort_and_peak_memory(
    const RenderGraph& graph,
    const std::vector<int>& output_degree,
    GraphPreflightReport& report);

void compute_aggregate_metrics(
    const RenderGraph& graph,
    int64_t canvas_area,
    GraphPreflightReport& report);

void check_topological_warnings(
    const RenderGraph& graph,
    GraphPreflightReport& report);

void check_asset_integrity(
    const RenderGraph& graph,
    GraphPreflightReport& report);

void check_coordinate_mismatch(
    const RenderGraph& graph,
    const std::vector<GraphNodeId>& topo_order,
    GraphPreflightReport& report);

void propagate_dirty_chain(
    const RenderGraph& graph,
    const std::vector<GraphNodeId>& topo_order,
    GraphPreflightReport& report);

} // namespace chronon3d::graph

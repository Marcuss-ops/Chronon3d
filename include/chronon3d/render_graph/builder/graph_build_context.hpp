#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/render/resolved_types.hpp>

#include <memory_resource>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace chronon3d {

class Scene;

namespace graph {

/// Shared mutable state for the graph build pipeline.
///
/// This struct is created once per `GraphBuilder::build()` call and passed
/// to every `GraphBuildPass::run()`.  Passes read from and write to this
/// context so that later passes can see the results of earlier ones.
struct GraphBuildContext {
    // ── Inputs (set before pipeline runs) ──────────────────────────────
    const Scene*                      scene{nullptr};
    RenderGraphContext*               render_ctx{nullptr};   // mutable
    RenderGraph*                      graph{nullptr};

    // ── Resolved layer data (populated by resolve_layers or pre-populated)
    struct ResolvedData {
        std::pmr::vector<ResolvedLayer> layers;
        ResolvedCamera                  camera;
    } resolved;

    /// When true, the ResolvePass will skip calling resolve_layers()
    /// and use the pre-populated resolved data instead.
    bool resolved_prepopulated{false};

    // ── Layer metadata (populated by passes) ───────────────────────────
    Camera2_5DRuntime                             cam25d{};
    std::unordered_map<std::string, bool>         is_static_cache;
    std::unordered_set<std::string>               matte_source_names;
    std::unordered_map<std::string, const ResolvedLayer*> name_to_resolved;

    // ── Current graph output (updated by passes) ───────────────────────
    GraphNodeId current_output{k_invalid_node};
};

} // namespace graph
} // namespace chronon3d

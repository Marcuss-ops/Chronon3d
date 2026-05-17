#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <future>

namespace chronon3d::graph {

class GraphExecutor {
public:
    GraphExecutor();

    std::shared_ptr<Framebuffer> execute(
        RenderGraph& graph,
        GraphNodeId output,
        RenderGraphContext& ctx
    );

    std::shared_ptr<Framebuffer> execute(RenderGraph& graph, RenderGraphContext& ctx) {
        return execute(graph, graph.output(), ctx);
    }

private:
    std::unordered_map<GraphNodeId, std::shared_ptr<Framebuffer>> m_temp;
    std::unordered_map<GraphNodeId, u64> m_resolved_key_digest;
    std::unordered_map<GraphNodeId, std::shared_ptr<std::promise<std::shared_ptr<Framebuffer>>>> m_pending;
    mutable std::mutex m_mutex;

    std::shared_ptr<Framebuffer> execute_node(
        RenderGraph& graph,
        GraphNodeId id,
        RenderGraphContext& ctx
    );
};

} // namespace chronon3d::graph

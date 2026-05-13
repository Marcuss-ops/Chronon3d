#pragma once

#include <chronon3d/render_graph/render_node.hpp>
#include <utility>

namespace chronon3d::render_graph {

class RenderGraph {
public:
    using Native = chronon3d::rendergraph::RenderGraph;

    [[nodiscard]] NodeId add_output(std::string label, RenderCacheKey key) {
        return m_graph.add_output(std::move(label), std::move(key));
    }

    [[nodiscard]] NodeId add_transform(std::string label,
                                       RenderCacheKey key,
                                       NodeId input,
                                       Transform transform,
                                       RenderState base_state) {
        return m_graph.add_transform(std::move(label), std::move(key), input,
                                     std::move(transform), std::move(base_state));
    }

    [[nodiscard]] NodeId add_source(std::string label,
                                    RenderCacheKey key,
                                    NodeId input,
                                    const ::chronon3d::RenderNode& node) {
        return m_graph.add_source(std::move(label), std::move(key), input, node);
    }

    [[nodiscard]] NodeId add_layer_source(std::string label,
                                          RenderCacheKey key,
                                          NodeId input,
                                          const Layer& layer) {
        return m_graph.add_layer_source(std::move(label), std::move(key), input, layer);
    }

    [[nodiscard]] NodeId add_effect(std::string label,
                                    RenderCacheKey key,
                                    NodeId input,
                                    const Layer& layer) {
        return m_graph.add_effect(std::move(label), std::move(key), input, layer);
    }

    [[nodiscard]] NodeId add_composite(std::string label,
                                       RenderCacheKey key,
                                       NodeId bottom,
                                       NodeId top,
                                       BlendMode mode) {
        return m_graph.add_composite(std::move(label), std::move(key), bottom, top, mode);
    }

    [[nodiscard]] std::string debug_dot() const { return m_graph.debug_dot(); }

    [[nodiscard]] std::unique_ptr<Framebuffer> execute(RenderGraphExecutionContext& ctx) const {
        return m_graph.execute(ctx);
    }

    [[nodiscard]] bool empty() const { return m_graph.empty(); }
    [[nodiscard]] usize size() const { return m_graph.size(); }

    [[nodiscard]] const Native& native() const { return m_graph; }
    [[nodiscard]] Native& native() { return m_graph; }

private:
    Native m_graph;
};

} // namespace chronon3d::render_graph

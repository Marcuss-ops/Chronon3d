#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <utility>

namespace chronon3d::render_graph {

class GraphBuilder {
public:
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

    [[nodiscard]] RenderGraph build() && { return std::move(m_graph); }

    [[nodiscard]] const RenderGraph& graph() const { return m_graph; }

private:
    RenderGraph m_graph;
};

} // namespace chronon3d::render_graph

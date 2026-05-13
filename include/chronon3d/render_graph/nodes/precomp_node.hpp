#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/render_graph/graph_executor.hpp>
#include <chronon3d/render_graph/graph_builder.hpp>
#include <chronon3d/core/composition_registry.hpp>

namespace chronon3d::graph {

class PrecompNode final : public RenderGraphNode {
public:
    PrecompNode(std::string comp_name, Frame start_frame, Frame duration)
        : m_comp_name(std::move(comp_name)), m_start_frame(start_frame), m_duration(duration) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Precomp; }
    std::string name() const override { return "Precomp:" + m_comp_name; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "precomp",
            .frame = ctx.frame - m_start_frame, // Nested frame time
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = rendergraph::hash_string(m_comp_name)
        };
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, const std::vector<std::shared_ptr<Framebuffer>>&) override {
        if (!ctx.registry || !ctx.registry->contains(m_comp_name)) {
            return std::make_shared<Framebuffer>(ctx.width, ctx.height);
        }

        // 1. Calculate nested time
        Frame nested_frame = ctx.frame - m_start_frame;
        if (nested_frame < 0 || (m_duration > 0 && nested_frame >= m_duration)) {
             auto fb = std::make_shared<Framebuffer>(ctx.width, ctx.height);
             fb->clear(Color::transparent());
             return fb;
        }

        // 2. Fetch/Evaluate nested composition
        auto comp = ctx.registry->create(m_comp_name);
        Scene nested_scene = comp.evaluate(nested_frame);

        // 3. Build nested graph
        RenderGraphContext nested_ctx = ctx;
        nested_ctx.frame = nested_frame;
        // nested_ctx.time_seconds = ...
        
        RenderGraph nested_graph = GraphBuilder::build(nested_scene, nested_ctx);

        // 4. Execute nested graph
        GraphExecutor executor;
        // We use the last node as output
        if (nested_graph.size() == 0) return std::make_shared<Framebuffer>(ctx.width, ctx.height);
        
        return executor.execute(nested_graph, static_cast<GraphNodeId>(nested_graph.size() - 1), nested_ctx);
    }

private:
    std::string m_comp_name;
    Frame m_start_frame{0};
    Frame m_duration{-1};
};

} // namespace chronon3d::graph

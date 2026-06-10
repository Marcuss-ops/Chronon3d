#pragma once

#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <span>

namespace chronon3d::graph {

class PrecompNode final : public RenderGraphNode {
public:
    PrecompNode(std::string comp_name, Frame start_frame, Frame duration, Frame cache_frame = Frame{-1})
        : m_comp_name(std::move(comp_name)), m_start_frame(start_frame), m_duration(duration), m_cache_frame(cache_frame) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Precomp; }
    std::string name() const override { return "Precomp:" + m_comp_name; }

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "precomp",
            .frame = m_cache_frame >= 0 ? m_cache_frame : (frame_dependent() ? (ctx.frame.frame.frame - m_start_frame) : Frame{0}), // Nested frame time
            .width = ctx.frame.frame.width,
            .height = ctx.frame.frame.height,
            .params_hash = hash_string(m_comp_name)
        };
    }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> = {}
    ) const override {
        return raster::BBox{0, 0, ctx.frame.frame.width, ctx.frame.frame.height};
    }

    OwnedFB execute(RenderGraphContext& ctx, std::span<const FramebufferRef>, std::span<const std::optional<raster::BBox>>) override {
        if (!ctx.resources.registry || !ctx.resources.registry->contains(m_comp_name)) {
            return ctx.acquire_owned_fb(ctx.frame.frame.width, ctx.frame.frame.height);
        }

        // 1. Calculate nested time
        Frame nested_frame = ctx.frame.frame.frame - m_start_frame;
        
        if (nested_frame < 0 || (m_duration > 0 && nested_frame >= m_duration)) {
             return ctx.acquire_owned_fb(ctx.frame.frame.width, ctx.frame.frame.height);
        }

        // 2. Fetch nested composition
        auto comp = ctx.resources.registry->create(m_comp_name);

        // 3. Build nested graph context
        RenderGraphContext nested_ctx = ctx;
        nested_ctx.frame.frame = nested_frame;
        nested_ctx.frame.frame.width = comp.width();
        nested_ctx.frame.frame.height = comp.height();
        nested_ctx.camera.camera = comp.camera;
        
        Scene nested_scene = comp.evaluate(nested_frame);
        RenderGraph nested_graph = GraphBuilder::build(nested_scene, nested_ctx);

        // 4. Execute nested graph
        GraphExecutor executor;
        if (!nested_graph.has_output()) {
            return ctx.acquire_owned_fb(ctx.frame.frame.width, ctx.frame.frame.height);
        }

        auto nested_result = executor.execute(nested_graph, nested_graph.output(), nested_ctx);
        if (!nested_result) {
            return ctx.acquire_owned_fb(ctx.frame.frame.width, ctx.frame.frame.height);
        }
        return ctx.acquire_owned_fb(*nested_result);
    }

private:
    std::string m_comp_name;
    Frame m_start_frame{0};
    Frame m_duration{-1};
    Frame m_cache_frame{-1};
};

} // namespace chronon3d::graph

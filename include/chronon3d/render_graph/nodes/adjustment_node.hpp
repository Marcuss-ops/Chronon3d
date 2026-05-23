#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <span>

namespace chronon3d::graph {

class AdjustmentNode final : public RenderGraphNode {
public:
    explicit AdjustmentNode(EffectStack effects) : m_effects(std::move(effects)) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Adjustment; }
    std::string name() const override { return "Adjustment"; }

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "adjustment",
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = hash_effect_stack(m_effects)
        };
    }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext&,
        std::span<const std::optional<raster::BBox>> input_bboxes = {}
    ) const override {
        if (input_bboxes.empty()) return std::nullopt;
        return input_bboxes[0];
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, std::span<const std::shared_ptr<Framebuffer>> inputs, std::span<const std::optional<raster::BBox>>) override {
        if (inputs.empty()) return ctx.acquire_framebuffer(ctx.width, ctx.height);
        
        auto result = ctx.acquire_framebuffer(*inputs[0]);
        if (ctx.backend) {
            ctx.backend->apply_effect_stack(*result, m_effects, ctx.time_seconds, ctx.clip_rect);
            if (ctx.counters) {
                ctx.counters->effect_stack_calls.fetch_add(1, std::memory_order_relaxed);
                ctx.counters->effect_pixels.fetch_add(static_cast<uint64_t>(ctx.width * ctx.height), std::memory_order_relaxed);
            }
        }
        return result;
    }

private:
    EffectStack m_effects;
};

} // namespace chronon3d::graph

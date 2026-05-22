#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>

namespace chronon3d::graph {

class EffectStackNode final : public RenderGraphNode {
public:
    explicit EffectStackNode(EffectStack effects, Frame cache_frame = Frame{-1})
        : m_effects(std::move(effects)), m_cache_frame(cache_frame) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Effect; }
    std::string name() const override { return "EffectStack"; }

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "effect_stack",
            .frame = m_cache_frame >= 0 ? m_cache_frame : ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = hash_effect_stack(m_effects)
        };
    }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        const std::vector<std::optional<raster::BBox>>& input_bboxes = {}
    ) const override {
        if (input_bboxes.empty() || !input_bboxes[0]) {
            return std::nullopt;
        }
        auto bbox = *input_bboxes[0];
        if (bbox.is_empty()) {
            return bbox;
        }
        const f32 spread = compute_max_effect_spread();
        if (spread <= 0.0f) {
            return bbox;
        }
        bbox.x0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.x0) - spread)));
        bbox.y0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.y0) - spread)));
        bbox.x1 = std::min(ctx.width, static_cast<i32>(std::ceil(static_cast<f32>(bbox.x1) + spread)));
        bbox.y1 = std::min(ctx.height, static_cast<i32>(std::ceil(static_cast<f32>(bbox.y1) + spread)));
        if (bbox.is_empty()) {
            return std::nullopt;
        }
        return bbox;
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, const std::vector<std::shared_ptr<Framebuffer>>& inputs, const std::vector<std::optional<raster::BBox>>&) override {
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
    [[nodiscard]] f32 compute_max_effect_spread() const {
        f32 max_spread = 0.0f;
        for (const auto& inst : m_effects) {
            if (!inst.enabled) continue;

            if (auto* p = std::any_cast<BlurParams>(&inst.params)) {
                max_spread = std::max(max_spread, p->radius);
            } else if (auto* p = std::any_cast<DropShadowParams>(&inst.params)) {
                max_spread = std::max(max_spread,
                    std::max(std::abs(p->offset.x), std::abs(p->offset.y)) + p->radius);
            } else if (auto* p = std::any_cast<GlowParams>(&inst.params)) {
                max_spread = std::max(max_spread, p->radius);
            } else if (auto* p = std::any_cast<BloomParams>(&inst.params)) {
                max_spread = std::max(max_spread, p->radius);
            } else if (auto* p = std::any_cast<Fake3DWaveParams>(&inst.params)) {
                f32 s = p->depth_px + p->amplitude_px;
                if (p->shadow_enabled) {
                    s += std::max(std::abs(p->shadow_offset.x),
                                  std::abs(p->shadow_offset.y)) + p->shadow_blur;
                }
                if (p->expand_bounds) {
                    max_spread = std::max(max_spread, s);
                }
            } else if (auto* vp = std::any_cast<EffectParams>(&inst.params)) {
                if (auto* p = std::get_if<BlurParams>(vp)) {
                    max_spread = std::max(max_spread, p->radius);
                } else if (auto* p = std::get_if<DropShadowParams>(vp)) {
                    max_spread = std::max(max_spread,
                        std::max(std::abs(p->offset.x), std::abs(p->offset.y)) + p->radius);
                } else if (auto* p = std::get_if<GlowParams>(vp)) {
                    max_spread = std::max(max_spread, p->radius);
                } else if (auto* p = std::get_if<BloomParams>(vp)) {
                    max_spread = std::max(max_spread, p->radius);
                } else if (auto* p = std::get_if<Fake3DWaveParams>(vp)) {
                    f32 s = p->depth_px + p->amplitude_px;
                    if (p->shadow_enabled) {
                        s += std::max(std::abs(p->shadow_offset.x),
                                      std::abs(p->shadow_offset.y)) + p->shadow_blur;
                    }
                    if (p->expand_bounds) {
                        max_spread = std::max(max_spread, s);
                    }
                }
            }
        }
        // Add 2px safety margin for anti-aliasing fringes
        return max_spread > 0.0f ? max_spread + 2.0f : 0.0f;
    }

    EffectStack m_effects;
    Frame m_cache_frame{-1};
};

} // namespace chronon3d::graph

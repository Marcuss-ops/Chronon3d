#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <span>

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
            .frame = m_cache_frame >= 0 ? m_cache_frame : (frame_dependent() ? ctx.frame : Frame{0}),
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = hash_effect_stack(m_effects)
        };
    }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> input_bboxes = {}
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
            return bbox;
        }
        return bbox;
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, std::span<const std::shared_ptr<Framebuffer>> inputs, std::span<const std::optional<raster::BBox>> input_bboxes) override {
        if (inputs.empty() || !inputs[0]) {
            auto empty = ctx.acquire_framebuffer(ctx.width, ctx.height);
            empty->clear(Color::transparent());
            return empty;
        }

        auto result = ctx.acquire_framebuffer(*inputs[0]);
        if (ctx.backend) {
            std::optional<raster::BBox> local_clip = ctx.clip_rect;
            auto pred_bbox = predicted_bbox(ctx, input_bboxes);
            if (pred_bbox) {
                if (local_clip) {
                    local_clip->x0 = std::max(local_clip->x0, pred_bbox->x0);
                    local_clip->y0 = std::max(local_clip->y0, pred_bbox->y0);
                    local_clip->x1 = std::min(local_clip->x1, pred_bbox->x1);
                    local_clip->y1 = std::min(local_clip->y1, pred_bbox->y1);
                } else {
                    local_clip = pred_bbox;
                }
            }
            ctx.backend->apply_effect_stack(*result, m_effects, ctx.time_seconds, local_clip);
            if (ctx.counters) {
                ctx.counters->effect_stack_calls.fetch_add(1, std::memory_order_relaxed);
                uint64_t area = static_cast<uint64_t>(ctx.width * ctx.height);
                if (local_clip) {
                    raster::BBox clipped = *local_clip;
                    clipped.clip_to(ctx.width, ctx.height);
                    if (!clipped.is_empty()) {
                        area = static_cast<uint64_t>(clipped.x1 - clipped.x0) * (clipped.y1 - clipped.y0);
                    } else {
                        area = 0;
                    }
                }
                ctx.counters->effect_pixels.fetch_add(area, std::memory_order_relaxed);
            }
        }
        return result;
    }

    // ── Accessors / mutators for graph optimization ────────────────────
    [[nodiscard]] const EffectStack& effects() const { return m_effects; }
    [[nodiscard]] EffectStack& effects() { return m_effects; }

    /// Prepend `prefix` effects before this node's own effects.
    /// Used by the optimizer to merge an upstream EffectStackNode into this one.
    void prepend_effects(const EffectStack& prefix) {
        m_effects.insert(m_effects.begin(), prefix.begin(), prefix.end());
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
            }
        }
        // Add 2px safety margin for anti-aliasing fringes
        return max_spread > 0.0f ? max_spread + 2.0f : 0.0f;
    }

    EffectStack m_effects;
    Frame m_cache_frame{-1};
};

} // namespace chronon3d::graph

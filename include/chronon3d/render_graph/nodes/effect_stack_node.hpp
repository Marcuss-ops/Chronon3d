#pragma once

#include <chronon3d/effects/glow_pipeline.hpp>
#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <span>

namespace chronon3d::graph {

class EffectStackNode final : public RenderGraphNode {
public:
    explicit EffectStackNode(EffectStack effects,
                             RenderNodeCachePolicy policy)
        : RenderGraphNode(policy), m_effects(std::move(effects)), m_cache_frame(Frame{-1}) {}

    explicit EffectStackNode(EffectStack effects,
                             Frame cache_frame = Frame{-1},
                             RenderNodeCachePolicy policy = static_memory_cache("effect_stack"))
        : RenderGraphNode(policy), m_effects(std::move(effects)), m_cache_frame(cache_frame) {}

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Effect; }
    std::string_view name() const noexcept override { return "EffectStack"; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "effect_stack",
            .frame = m_cache_frame >= 0 ? m_cache_frame : Frame{0},
            .width = ctx.frame_input.width,
            .height = ctx.frame_input.height,
            .params_hash = hash_effect_stack(m_effects)
        };
    }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> input_bboxes = {}
    ) const override;

    OwnedFB execute(RenderGraphContext& ctx, std::span<const FramebufferRef> inputs, std::span<const std::optional<raster::BBox>> input_bboxes) override;

    // ── Accessors / mutators for graph optimization ────────────────────
    [[nodiscard]] const EffectStack& effects() const { return m_effects; }
    [[nodiscard]] EffectStack& effects() { return m_effects; }

    /// Returns true if the effect stack contains only color/tonal effects
    /// (no blur, glow, shadow, bloom, or other spatially-expanding effects).
    /// Used by the graph builder for skip-when-opaque analysis.
    [[nodiscard]] bool is_color_only() const { return compute_max_effect_spread() <= 0.0f; }

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

            using enum effects::EffectType;
            switch (inst.effect_type) {
            case Blur: {
                if (auto* p = std::get_if<BlurParams>(&inst.params))
                    max_spread = std::max(max_spread, p->radius);
                break;
            }
            case DropShadow: {
                if (auto* p = std::get_if<DropShadowParams>(&inst.params))
                    max_spread = std::max(max_spread,
                        std::max(std::abs(p->offset.x), std::abs(p->offset.y)) + p->radius);
                break;
            }
            case Glow: {
                if (auto* p = std::get_if<GlowParams>(&inst.params))
                    max_spread = std::max(max_spread, glow_effect_extent(*p));
                break;
            }
            case Bloom: {
                if (auto* p = std::get_if<BloomParams>(&inst.params))
                    max_spread = std::max(max_spread, p->radius);
                break;
            }
            case Fake3DWave: {
                if (auto* p = std::get_if<Fake3DWaveParams>(&inst.params)) {
                    f32 s = p->depth_px + p->amplitude_px;
                    if (p->shadow_enabled) {
                        s += std::max(std::abs(p->shadow_offset.x),
                                      std::abs(p->shadow_offset.y)) + p->shadow_blur;
                    }
                    if (p->expand_bounds) {
                        max_spread = std::max(max_spread, s);
                    }
                }
                break;
            }
            default: break;
            }
        }
        // Add 2px safety margin for anti-aliasing fringes
        return max_spread > 0.0f ? max_spread + 2.0f : 0.0f;
    }

    EffectStack m_effects;
    Frame m_cache_frame{-1};
};

} // namespace chronon3d::graph

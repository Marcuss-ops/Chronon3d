#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>
#include <span>

namespace chronon3d::graph {

class AdjustmentNode final : public RenderGraphNode {
public:
    explicit AdjustmentNode(EffectStack effects, RenderNodeCachePolicy cache_policy = frame_variant_cache("adjustment"))
        : RenderGraphNode(std::move(cache_policy)),
          m_effects(std::move(effects)) {}

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Adjustment; }
    std::string_view name() const noexcept override { return "Adjustment"; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "adjustment",
            .frame = cache_policy().is_frame_variant() ? ctx.frame.frame : Frame{0},
            .width = ctx.frame.width,
            .height = ctx.frame.height,
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

    OwnedFB execute(RenderGraphContext& ctx, std::span<const FramebufferRef> inputs, std::span<const std::optional<raster::BBox>>) override {
        if (inputs.empty()) return ctx.acquire_owned_fb(ctx.frame.width, ctx.frame.height);
        
        auto result = ctx.acquire_owned_fb(*inputs[0]);
        if (ctx.resources.backend) {
            const effects::EffectExecutionContext effect_context{
                .time_seconds = ctx.frame.time_seconds,
                .frame = ctx.frame.frame,
                .clip = ctx.tile.clip_rect,
                .quality = effects::RenderQuality::Final,
                .diagnostics_enabled = ctx.options.diagnostics_enabled
            };
            ctx.resources.backend->apply_effect_stack(*result, m_effects, effect_context);
            if (ctx.telemetry.counters) {
                ctx.telemetry.counters->effect_stack_calls.fetch_add(1, std::memory_order_relaxed);
                ctx.telemetry.counters->effect_pixels.fetch_add(static_cast<uint64_t>(ctx.frame.width * ctx.frame.height), std::memory_order_relaxed);
            }
        }
        return result;
    }

    // ── Accessors / mutators for graph optimization ────────────────────
    [[nodiscard]] const EffectStack& effects() const { return m_effects; }
    [[nodiscard]] EffectStack& effects() { return m_effects; }

    /// Returns true if the effect stack contains only color/tonal effects
    /// (no blur, glow, shadow, bloom, or other spatially-expanding effects).
    /// Used by the graph builder for skip-when-opaque analysis.
    /// Adjustment nodes never expand geometry (predicted_bbox == input bbox),
    /// so they are always considered color-only from an opaque-skip perspective.
    [[nodiscard]] bool is_color_only() const { return true; }

    /// Prepend `prefix` effects before this node's own effects.
    /// Used by the optimizer to merge an upstream effect-like node into this one.
    void prepend_effects(const EffectStack& prefix) {
        m_effects.insert(m_effects.begin(), prefix.begin(), prefix.end());
    }

private:
    EffectStack m_effects;
};

} // namespace chronon3d::graph

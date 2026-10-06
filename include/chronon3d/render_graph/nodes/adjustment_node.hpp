#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <chronon3d/render_graph/nodes/native_effects.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>
#include <algorithm>
#include <limits>
#include <span>
#include <string>

namespace chronon3d::graph {

NodeExecResult execute_strict_native_adjustment_stack(
    RenderGraphContext&, const EffectStack&, std::span<const FramebufferRef>,
    std::string_view);

class AdjustmentNode final : public RenderGraphNode {
public:
    explicit AdjustmentNode(EffectStack effects,
                            RenderNodeCachePolicy policy = static_memory_cache("adjustment"))
        : RenderGraphNode(policy), m_effects(std::move(effects)) {}

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Adjustment; }
    std::string_view name() const noexcept override { return "Adjustment"; }
    bool has_compiled_recorder() const noexcept override { return true; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "adjustment",
            .frame = cache_policy().frame_dependent() ? ctx.frame_input.frame : Frame{0},
            .width = ctx.frame_input.width,
            .height = ctx.frame_input.height,
            .params_hash = m_temporal_history_key.empty()
                ? hash_effect_stack(m_effects)
                : hash_combine(hash_effect_stack(m_effects),
                               hash_string(m_temporal_history_key))
        };
    }

    [[nodiscard]] TemporalRequirements temporal_requirements() const noexcept override {
        std::uint64_t history_frames = 0;
        for (const auto& effect : m_effects) {
            if (!effect.enabled || effect.effect_type != effects::EffectType::Echo) continue;
            const auto* params = std::get_if<EchoParams>(&effect.params);
            if (!params) continue;
            history_frames = std::max(history_frames,
                static_cast<std::uint64_t>(params->echoes) * params->time_offset_frames);
        }
        return TemporalRequirements{.history_frames = static_cast<i32>(std::min<std::uint64_t>(
            history_frames, static_cast<std::uint64_t>(std::numeric_limits<i32>::max())))};
    }

    void set_temporal_history_key(std::string key) {
        m_temporal_history_key = std::move(key);
    }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext&,
        std::span<const std::optional<raster::BBox>> input_bboxes = {}
    ) const override {
        if (input_bboxes.empty()) return std::nullopt;
        return input_bboxes[0];
    }

    NodeExecResult execute(RenderGraphContext& ctx, std::span<const FramebufferRef> inputs, std::span<const std::optional<raster::BBox>> input_bboxes) override {
        if (inputs.empty()) return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);

        // Strict-GPU adjustments share the typed EffectStackNode executor so
        // ordered native stages stay on graph-owned surfaces. The adjustment
        // contract keeps the input extent fixed. Spatial stages are admitted
        // only when the shared native executor can preserve that extent; other
        // stages fail closed rather than using VulkanBackend's CPU fallback.
        if (ctx.policy.is_gpu_native_required()) {
            for (const auto& effect : m_effects) {
                if (!effect.enabled) continue;
                switch (effect.effect_type) {
                case effects::EffectType::Blur:
                case effects::EffectType::Bloom:
                case effects::EffectType::Glow:
                case effects::EffectType::Tint:
                case effects::EffectType::Brightness:
                case effects::EffectType::Contrast:
                case effects::EffectType::Saturation:
                case effects::EffectType::HueRotate:
                case effects::EffectType::Invert:
                case effects::EffectType::Exposure:
                case effects::EffectType::TemperatureTint:
                case effects::EffectType::LiftGammaGain:
                case effects::EffectType::Offset:
                case effects::EffectType::Noise:
                case effects::EffectType::Checkerboard:
                case effects::EffectType::Fill:
                case effects::EffectType::Levels:
                case effects::EffectType::Threshold:
                case effects::EffectType::Keying:
                case effects::EffectType::Vignette:
                case effects::EffectType::Mosaic:
                case effects::EffectType::Echo:
                    break;
                default:
                    return NodeExecutionError{
                        RenderBackendErrorCode::UnsupportedCapability,
                        "AdjustmentNode",
                        "strict Vulkan adjustment stack has no certified fixed-extent native route"};
                }
            }
            return execute_strict_native_adjustment_stack(
                ctx, m_effects, inputs, m_temporal_history_key);
        }
        
        auto result = ctx.acquire_owned_fb(*inputs[0]);
        if (ctx.backend) {
            const effects::EffectExecutionContext effect_context{
                .time_seconds = ctx.frame_input.time_seconds,
                .frame = ctx.frame_input.frame,
                .clip = ctx.node_exec.clip_rect,
                .quality = effects::RenderQuality::Final,
                .diagnostics_enabled = ctx.policy.diagnostics_enabled,
                .effect_processors = ctx.node_exec.current_effect_processors,
                .processor_snapshot = ctx.node_exec.processor_snapshot,
                .processors_resolved = ctx.node_exec.processor_bindings_compiled,
                .temporal_history_key = m_temporal_history_key
            };
            const bool native =
                native_effects::try_native_full_frame_blur(
                    ctx, m_effects, *result, ctx.node_exec.clip_rect,
                    inputs[0]->surface_handle(), inputs[0].get()) ||
                native_effects::try_native_full_frame_glow(
                    ctx, m_effects, *result, ctx.node_exec.clip_rect) ||
                native_effects::try_native_full_frame_tint(
                    ctx, m_effects, *result, ctx.node_exec.clip_rect);
            if (!native) {
                ctx.backend->apply_effect_stack(*result, m_effects, effect_context);
            }
            if (ctx.node_exec.counters) {
                ctx.node_exec.counters->effect_stack_calls.fetch_add(1, std::memory_order_relaxed);
                ctx.node_exec.counters->effect_pixels.fetch_add(static_cast<uint64_t>(ctx.frame_input.width * ctx.frame_input.height), std::memory_order_relaxed);
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
    std::string m_temporal_history_key;
};

} // namespace chronon3d::graph

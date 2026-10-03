#pragma once

#include <chronon3d/effects/glow_pipeline.hpp>
#include <chronon3d/effects/spatial_bounds.hpp>
#include <chronon3d/render_graph/nodes/native_effects.hpp>
#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

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
    bool has_compiled_recorder() const noexcept override { return true; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        const auto effect_hash = hash_effect_stack(m_effects);
        const auto params_hash = m_temporal_history_key.empty()
            ? effect_hash
            : hash_combine(effect_hash, hash_string(m_temporal_history_key));
        return cache::NodeCacheKey{
            .scope = "effect_stack",
            .frame = cache_frame_for_policy(
                cache_policy(), ctx.frame_input.frame, m_cache_frame),
            .width = ctx.frame_input.width,
            .height = ctx.frame_input.height,
            .params_hash = params_hash
        };
    }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> input_bboxes = {}
    ) const override;

    NodeExecResult execute(RenderGraphContext& ctx, std::span<const FramebufferRef> inputs, std::span<const std::optional<raster::BBox>> input_bboxes) override;

    // ── Accessors / mutators for graph optimization ────────────────────
    [[nodiscard]] const EffectStack& effects() const { return m_effects; }
    [[nodiscard]] EffectStack& effects() { return m_effects; }

    /// Returns true if the effect stack contains only color/tonal effects
    /// (no blur, glow, shadow, bloom, or other spatially-expanding effects).
    /// Used by the graph builder for skip-when-opaque analysis.
    [[nodiscard]] bool is_color_only() const {
        for (const auto& effect : m_effects)
            if (effect.enabled && (effect.effect_type == effects::EffectType::Echo ||
                                   effect.effect_type == effects::EffectType::Mosaic ||
                                   effect.effect_type == effects::EffectType::DisplacementMap)) return false;
        return compute_max_effect_spread() <= 0.0f;
    }

    [[nodiscard]] bool has_temporal_effect() const {
        for (const auto& effect : m_effects)
            if (effect.enabled && effect.effect_type == effects::EffectType::Echo) return true;
        return false;
    }

    [[nodiscard]] TemporalRequirements temporal_requirements() const noexcept override {
        std::uint64_t history_frames = 0;
        for (const auto& effect : m_effects) {
            if (!effect.enabled || effect.effect_type != effects::EffectType::Echo) continue;
            const auto* params = std::get_if<EchoParams>(&effect.params);
            if (!params) continue;
            const auto required = static_cast<std::uint64_t>(params->echoes) *
                                  params->time_offset_frames;
            history_frames = std::max(history_frames, required);
        }
        return TemporalRequirements{.history_frames = static_cast<i32>(std::min<std::uint64_t>(
            history_frames, static_cast<std::uint64_t>(std::numeric_limits<i32>::max())))};
    }

    /// Prepend `prefix` effects before this node's own effects.
    /// Used by the optimizer to merge an upstream EffectStackNode into this one.
    void prepend_effects(const EffectStack& prefix) {
        m_effects.insert(m_effects.begin(), prefix.begin(), prefix.end());
    }

    void set_temporal_history_key(std::string key) {
        m_temporal_history_key = std::move(key);
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
                    max_spread = std::max(max_spread,
                        effects::spatial_halo_extent(effects::effect_spatial_halo(*p)));
                break;
            }
            case DropShadow: {
                if (auto* p = std::get_if<DropShadowParams>(&inst.params))
                    {
                        const auto halo = effects::effect_spatial_halo(*p);
                        max_spread = std::max(max_spread,
                            std::max({halo.left, halo.top, halo.right, halo.bottom}));
                    }
                break;
            }
            case Offset: {
                if (auto* p = std::get_if<OffsetParams>(&inst.params)) {
                    const auto halo = effects::offset_spatial_halo(*p);
                    max_spread = std::max(max_spread,
                        std::max({halo.left, halo.top, halo.right, halo.bottom}));
                }
                break;
            }
            case DirectionalBlur: {
                if (auto* p = std::get_if<DirectionalBlurParams>(&inst.params)) {
                    const auto halo = effects::directional_blur_spatial_halo(*p);
                    max_spread = std::max(max_spread,
                        std::max({halo.left, halo.top, halo.right, halo.bottom}));
                }
                break;
            }
            case Glow: {
                if (auto* p = std::get_if<GlowParams>(&inst.params))
                    max_spread = std::max(max_spread,
                        effects::spatial_halo_extent(effects::effect_spatial_halo(*p)));
                break;
            }
            case Bloom: {
                if (auto* p = std::get_if<BloomParams>(&inst.params))
                    max_spread = std::max(max_spread,
                        effects::spatial_halo_extent(effects::effect_spatial_halo(*p)));
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
        return max_spread;
    }

    EffectStack m_effects;
    std::string m_temporal_history_key;
    Frame m_cache_frame{-1};
};

} // namespace chronon3d::graph

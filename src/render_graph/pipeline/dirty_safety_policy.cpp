// ---------------------------------------------------------------------------
// dirty_safety_policy.cpp — Dirty-rect and tile execution safety policies
// ---------------------------------------------------------------------------

#include "dirty_safety_policy.hpp"
#include "../builder/graph_builder_internal.hpp"
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/effects/effect_category.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/effects/effect_type.hpp>
#include <chronon3d/effects/glow_pipeline.hpp>
#include <cmath>

namespace chronon3d::graph::detail {

// ───── compute_layer_spatial_spread ─────────────────────────────────────
// Returns the maximum spatial expansion (in pixels) of any Blur or Light
// effect in the layer's effect stack.  Mirrors EffectStackNode's internal
// spread computation but operates on a Layer's effect stack directly so
// the dirty-rect pipeline doesn't need a graph node to consult safety.
[[nodiscard]] f32 compute_layer_spatial_spread(const Layer& layer) {
    f32 max_spread = 0.0f;
    for (const auto& inst : layer.effects) {
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
                if (p->expand_bounds) {
                    f32 s = p->depth_px + p->amplitude_px;
                    if (p->shadow_enabled) {
                        s += std::max(std::abs(p->shadow_offset.x),
                                      std::abs(p->shadow_offset.y)) + p->shadow_blur;
                    }
                    max_spread = std::max(max_spread, s);
                }
            }
            break;
        }
        default: break;
        }
    }
    // 2px safety margin for anti-aliasing fringes (matches EffectStackNode)
    return max_spread > 0.0f ? max_spread + 2.0f : 0.0f;
}

bool is_safe_for_dirty_rects(const Layer& layer, bool motion_blur_enabled,
                              const effects::EffectCatalog* catalog) {
    if (motion_blur_enabled) return false;
    if (layer.blend_mode != BlendMode::Normal) return false;
    if (layer.mask.enabled()) return false;

    for (const auto& eff : layer.effects) {
        if (!eff.enabled) continue;

        effects::EffectCategory category = eff.descriptor.category;
        if (catalog && catalog->contains(eff.descriptor.id)) {
            category = catalog->get(eff.descriptor.id).category;
        }

        // Distort + Temporal: geometry warp / time evolution cannot be
        // predicted from bbox alone — bail to full-frame.
        //
        // Blur + Light are now ALLOWED — caller dilates bbox via
        // compute_layer_spatial_spread() to track partial-dirty.
        if (category == effects::EffectCategory::Distort)  return false;
        if (category == effects::EffectCategory::Temporal) return false;
    }
    return true;
}

bool has_layer_with_spatial_effects(
    const detail::LayerResolutionResult& resolved,
    Frame frame,
    const effects::EffectCatalog* catalog
) {
    for (const auto& rl : resolved.layers) {
        if (!rl.layer || !rl.layer->active_at(frame)) continue;
        for (const auto& eff : rl.layer->effects) {
            if (!eff.enabled) continue;
            effects::EffectCategory category = eff.descriptor.category;
            if (catalog && catalog->contains(eff.descriptor.id)) {
                category = catalog->get(eff.descriptor.id).category;
            }
            if (category == effects::EffectCategory::Blur)     return true;
            if (category == effects::EffectCategory::Distort)  return true;
            if (category == effects::EffectCategory::Temporal) return true;
            if (category == effects::EffectCategory::Light)    return true;
        }
    }
    return false;
}

} // namespace chronon3d::graph::detail

// ---------------------------------------------------------------------------
// dirty_safety_policy.cpp — Dirty-rect and tile execution safety policies
// ---------------------------------------------------------------------------

#include "dirty_safety_policy.hpp"
#include "../builder/graph_builder_internal.hpp"
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/effects/effect_category.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/effects/effect_type.hpp>
#include <chronon3d/effects/glow_pipeline.hpp>
#include <chronon3d/effects/spatial_bounds.hpp>
#include <cmath>

namespace chronon3d::graph::detail {

// ───── compute_layer_spatial_spread ─────────────────────────────────────
// Returns the maximum spatial expansion (in pixels) of any Blur or Light
// effect in the layer's effect stack.  Mirrors EffectStackNode's internal
// spread computation but operates on a Layer's effect stack directly so
// the dirty-rect pipeline doesn't need a graph node to consult safety.
[[nodiscard]] f32 compute_layer_spatial_spread(const Layer& layer) {
    f32 max_spread = 0.0f;
    for (const auto& inst : layer.effects()) {
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

LayerSpatialHalo compute_layer_spatial_halo(const Layer& layer) {
    LayerSpatialHalo halo;
    for (const auto& inst : layer.effects()) {
        if (!inst.enabled) continue;
        using enum effects::EffectType;
        switch (inst.effect_type) {
        case Blur:
            if (const auto* p = std::get_if<BlurParams>(&inst.params)) {
                halo.left = std::max(halo.left, p->radius);
                halo.top = std::max(halo.top, p->radius);
                halo.right = std::max(halo.right, p->radius);
                halo.bottom = std::max(halo.bottom, p->radius);
            }
            break;
        case DropShadow:
            if (const auto* p = std::get_if<DropShadowParams>(&inst.params)) {
                const auto effect_halo = effects::drop_shadow_spatial_halo(*p);
                halo.left = std::max(halo.left, effect_halo.left);
                halo.top = std::max(halo.top, effect_halo.top);
                halo.right = std::max(halo.right, effect_halo.right);
                halo.bottom = std::max(halo.bottom, effect_halo.bottom);
            }
            break;
        case Offset:
            if (const auto* p = std::get_if<OffsetParams>(&inst.params)) {
                const auto effect_halo = effects::offset_spatial_halo(*p);
                halo.left = std::max(halo.left, effect_halo.left);
                halo.top = std::max(halo.top, effect_halo.top);
                halo.right = std::max(halo.right, effect_halo.right);
                halo.bottom = std::max(halo.bottom, effect_halo.bottom);
            }
            break;
        case DirectionalBlur:
            if (const auto* p = std::get_if<DirectionalBlurParams>(&inst.params)) {
                const auto effect_halo = effects::directional_blur_spatial_halo(*p);
                halo.left = std::max(halo.left, effect_halo.left);
                halo.top = std::max(halo.top, effect_halo.top);
                halo.right = std::max(halo.right, effect_halo.right);
                halo.bottom = std::max(halo.bottom, effect_halo.bottom);
            }
            break;
        case Glow:
            if (const auto* p = std::get_if<GlowParams>(&inst.params)) {
                const auto extent = glow_effect_extent(*p);
                halo.left = std::max(halo.left, extent);
                halo.top = std::max(halo.top, extent);
                halo.right = std::max(halo.right, extent);
                halo.bottom = std::max(halo.bottom, extent);
            }
            break;
        case Bloom:
            if (const auto* p = std::get_if<BloomParams>(&inst.params)) {
                halo.left = std::max(halo.left, p->radius);
                halo.top = std::max(halo.top, p->radius);
                halo.right = std::max(halo.right, p->radius);
                halo.bottom = std::max(halo.bottom, p->radius);
            }
            break;
        case Fake3DWave:
            if (const auto* p = std::get_if<Fake3DWaveParams>(&inst.params)) {
                if (p->expand_bounds) {
                    auto extent = p->depth_px + p->amplitude_px;
                    if (p->shadow_enabled) {
                        extent += p->shadow_blur;
                        halo.left = std::max(halo.left,
                            extent + std::max(0.0f, -p->shadow_offset.x));
                        halo.right = std::max(halo.right,
                            extent + std::max(0.0f, p->shadow_offset.x));
                        halo.top = std::max(halo.top,
                            extent + std::max(0.0f, -p->shadow_offset.y));
                        halo.bottom = std::max(halo.bottom,
                            extent + std::max(0.0f, p->shadow_offset.y));
                    } else {
                        halo.left = std::max(halo.left, extent);
                        halo.top = std::max(halo.top, extent);
                        halo.right = std::max(halo.right, extent);
                        halo.bottom = std::max(halo.bottom, extent);
                    }
                }
            }
            break;
        default:
            break;
        }
    }
    if (halo.left > 0.0f || halo.top > 0.0f ||
        halo.right > 0.0f || halo.bottom > 0.0f) {
        halo.left += 2.0f;
        halo.top += 2.0f;
        halo.right += 2.0f;
        halo.bottom += 2.0f;
    }
    return halo;
}

bool is_safe_for_dirty_rects(const Layer& layer, bool motion_blur_enabled,
                              const effects::EffectCatalog* catalog) {
    if (motion_blur_enabled) return false;
    if (layer.blend_mode != BlendMode::Normal) return false;
    if (layer.mask.enabled()) return false;

    for (const auto& eff : layer.effects()) {
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
        for (const auto& eff : rl.layer->effects()) {
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

// ---------------------------------------------------------------------------
// dirty_safety_policy.cpp — Dirty-rect and tile execution safety policies
// ---------------------------------------------------------------------------

#include "dirty_safety_policy.hpp"
#include "../builder/graph_builder_internal.hpp"
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/effects/effect_category.hpp>
#include <chronon3d/effects/effect_registry.hpp>

namespace chronon3d::graph::detail {

bool is_safe_for_dirty_rects(const Layer& layer, bool motion_blur_enabled) {
    if (motion_blur_enabled) return false;
    if (layer.blend_mode != BlendMode::Normal) return false;
    if (layer.mask.enabled()) return false;

    const auto& registry = effects::EffectRegistry::instance();
    for (const auto& eff : layer.effects) {
        if (!eff.enabled) continue;

        effects::EffectCategory category = eff.descriptor.category;
        if (registry.contains(eff.descriptor.id)) {
            category = registry.get(eff.descriptor.id).category;
        }

        // Blur, distort, temporal, and light effects (glow/bloom/shadow)
        // expand the effective bbox beyond the geometric bounds — dirty
        // rects and tile execution cannot track them without causing seams
        // at tile boundaries.
        if (category == effects::EffectCategory::Blur)     return false;
        if (category == effects::EffectCategory::Distort)  return false;
        if (category == effects::EffectCategory::Temporal) return false;
        if (category == effects::EffectCategory::Light)    return false;
    }
    return true;
}

bool has_layer_with_spatial_effects(
    const detail::LayerResolutionResult& resolved,
    Frame frame
) {
    const auto& registry = effects::EffectRegistry::instance();
    for (const auto& rl : resolved.layers) {
        if (!rl.layer || !rl.layer->active_at(frame)) continue;
        for (const auto& eff : rl.layer->effects) {
            if (!eff.enabled) continue;
            effects::EffectCategory category = eff.descriptor.category;
            if (registry.contains(eff.descriptor.id)) {
                category = registry.get(eff.descriptor.id).category;
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

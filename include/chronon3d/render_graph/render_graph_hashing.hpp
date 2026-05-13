#pragma once

#include <chronon3d/scene/effect_stack.hpp>
#include <chronon3d/scene/mask.hpp>
#include <chronon3d/renderer/render_graph.hpp> // For existing hash_combine etc.
#include <xxhash.h>

namespace chronon3d::graph {

// Re-using existing hashing utils from rendergraph namespace
using rendergraph::hash_combine;
using rendergraph::hash_value;
using rendergraph::hash_bytes;

inline u64 hash_blur_params(const BlurParams& p) {
    return hash_value(p.radius);
}

inline u64 hash_tint_params(const TintParams& p) {
    u64 seed = rendergraph::hash_color(p.color);
    return hash_combine(seed, hash_value(p.amount));
}

inline u64 hash_brightness_params(const BrightnessParams& p) {
    return hash_value(p.value);
}

inline u64 hash_contrast_params(const ContrastParams& p) {
    return hash_value(p.value);
}

inline u64 hash_drop_shadow_params(const DropShadowParams& p) {
    u64 seed = rendergraph::hash_vec2(p.offset);
    seed = hash_combine(seed, rendergraph::hash_color(p.color));
    return hash_combine(seed, hash_value(p.radius));
}

inline u64 hash_glow_params(const GlowParams& p) {
    u64 seed = hash_value(p.radius);
    seed = hash_combine(seed, hash_value(p.intensity));
    return hash_combine(seed, rendergraph::hash_color(p.color));
}

inline u64 hash_effect_params(const EffectParams& p) {
    return std::visit([](const auto& params) -> u64 {
        using T = std::decay_t<decltype(params)>;
        if constexpr (std::is_same_v<T, BlurParams>) return hash_combine(1, hash_blur_params(params));
        if constexpr (std::is_same_v<T, TintParams>) return hash_combine(2, hash_tint_params(params));
        if constexpr (std::is_same_v<T, BrightnessParams>) return hash_combine(3, hash_brightness_params(params));
        if constexpr (std::is_same_v<T, ContrastParams>) return hash_combine(4, hash_contrast_params(params));
        if constexpr (std::is_same_v<T, DropShadowParams>) return hash_combine(5, hash_drop_shadow_params(params));
        if constexpr (std::is_same_v<T, GlowParams>) return hash_combine(6, hash_glow_params(params));
        return 0;
    }, p);
}

inline u64 hash_effect_stack(const EffectStack& effects) {
    u64 seed = 0;
    for (const auto& e : effects) {
        if (!e.enabled) continue;
        seed = hash_combine(seed, hash_effect_params(e.params));
    }
    return seed;
}

inline u64 hash_mask(const Mask& m) {
    if (!m.enabled()) return 0;
    u64 seed = hash_value(static_cast<int>(m.type));
    seed = hash_combine(seed, rendergraph::hash_vec3(m.pos));
    seed = hash_combine(seed, rendergraph::hash_vec2(m.size));
    seed = hash_combine(seed, hash_value(m.radius));
    seed = hash_combine(seed, hash_value(m.inverted));
    return seed;
}

} // namespace chronon3d::graph

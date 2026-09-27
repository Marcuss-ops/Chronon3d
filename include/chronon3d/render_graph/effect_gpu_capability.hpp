#pragma once

#include <chronon3d/effects/effect_type.hpp>

#include <array>
#include <string_view>

namespace chronon3d::graph {

enum class GpuEffectRoute : unsigned char {
    NativeZeroReadback,
    Unsupported,
};

struct GpuEffectCapabilityRow {
    effects::EffectType effect;
    GpuEffectRoute route;
    std::string_view diagnostic;
};

// Canonical strict-Vulkan capability authority for the effect-stack native
// full-frame dispatches. Unsupported rows fail before software fallback.
inline constexpr std::array<GpuEffectCapabilityRow, 5>
    kGpuEffectCapabilityRows{{
        {effects::EffectType::Blur, GpuEffectRoute::NativeZeroReadback,
         "Vulkan separable blur dispatch"},
        {effects::EffectType::Glow, GpuEffectRoute::NativeZeroReadback,
         "Vulkan glow dispatch"},
        {effects::EffectType::DropShadow, GpuEffectRoute::Unsupported,
         "Vulkan drop-shadow kernel is not implemented"},
        {effects::EffectType::Bloom, GpuEffectRoute::NativeZeroReadback,
         "Vulkan bright-pass and separable bloom dispatch"},
        {effects::EffectType::Tint, GpuEffectRoute::NativeZeroReadback,
         "Vulkan tint dispatch"},
    }};

[[nodiscard]] constexpr const GpuEffectCapabilityRow*
gpu_effect_capability(effects::EffectType effect) noexcept {
    for (const auto& row : kGpuEffectCapabilityRows)
        if (row.effect == effect) return &row;
    return nullptr;
}

} // namespace chronon3d::graph

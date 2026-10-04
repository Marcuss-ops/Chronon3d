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

namespace gpu_capability_detail {

struct NativeRoute {
    effects::EffectType effect;
    std::string_view diagnostic;
};

// Effects with a certified native Vulkan implementation.  Everything else in
// the catalog is Unsupported and fails closed under strict native-GPU policy.
inline constexpr std::array<NativeRoute, 14> kNativeRoutes{{
    {effects::EffectType::Blur, "Vulkan separable blur dispatch"},
    {effects::EffectType::Glow, "Vulkan glow dispatch"},
    {effects::EffectType::Bloom, "Vulkan bright-pass and separable bloom dispatch"},
    {effects::EffectType::Tint, "Vulkan tint dispatch"},
    {effects::EffectType::Brightness, "Vulkan color-matrix dispatch (brightness)"},
    {effects::EffectType::Contrast, "Vulkan color-matrix dispatch (contrast)"},
    {effects::EffectType::Saturation, "Vulkan color-matrix dispatch (saturation)"},
    {effects::EffectType::HueRotate, "Vulkan color-matrix dispatch (hue rotate)"},
    {effects::EffectType::Invert, "Vulkan color-matrix dispatch (invert)"},
    {effects::EffectType::Exposure, "Vulkan color-matrix dispatch (exposure)"},
    {effects::EffectType::Fill, "Vulkan color-matrix dispatch (fill)"},
    {effects::EffectType::Levels, "Vulkan color-matrix dispatch (levels)"},
    {effects::EffectType::Threshold, "Vulkan color-matrix dispatch (threshold)"},
    {effects::EffectType::Keying, "Vulkan color-matrix dispatch (luma/chroma key)"},
}};

[[nodiscard]] constexpr GpuEffectCapabilityRow make_row(effects::EffectType effect) {
    for (const auto& native : kNativeRoutes)
        if (native.effect == effect)
            return {effect, GpuEffectRoute::NativeZeroReadback, native.diagnostic};
    if (effect == effects::EffectType::DropShadow)
        return {effect, GpuEffectRoute::Unsupported,
                "Vulkan drop-shadow kernel is not implemented (software uses "
                "box/downsampled blur with no matching native kernel)"};
    return {effect, GpuEffectRoute::Unsupported,
            "Vulkan native kernel for this effect is not implemented"};
}

} // namespace gpu_capability_detail

// Canonical strict-Vulkan capability authority for the effect-stack native
// full-frame dispatches.  Rows are generated from effect_catalog.def so every
// catalog EffectType is accounted for exactly once; Unsupported rows fail
// before software fallback.
inline constexpr auto kGpuEffectCapabilityRows = std::to_array<GpuEffectCapabilityRow>({
#define CHRONON_EFFECT(val, enum_name, ...) \
    gpu_capability_detail::make_row(effects::EffectType::enum_name),
#include <chronon3d/effects/effect_catalog.def>
#undef CHRONON_EFFECT
});

[[nodiscard]] constexpr const GpuEffectCapabilityRow*
gpu_effect_capability(effects::EffectType effect) noexcept {
    for (const auto& row : kGpuEffectCapabilityRows)
        if (row.effect == effect) return &row;
    return nullptr;
}

} // namespace chronon3d::graph

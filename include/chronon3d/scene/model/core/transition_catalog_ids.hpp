#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace chronon3d {

inline constexpr std::array<std::string_view, 10> kLayerTransitionIds{
    "none", "crossfade", "slide", "zoom", "wipe_linear", "smooth_wipe",
    "circle_iris", "flash", "procedural_remotion", "remotion"};

[[nodiscard]] inline constexpr bool is_layer_transition_id(std::string_view id) noexcept {
    for (const auto candidate : kLayerTransitionIds)
        if (candidate == id) return true;
    return false;
}

/// Typed id for a built-in layer transition.
///
/// This is the single id→kind mapping: consumers that must branch on which
/// transition they hold (parameter decoding, default-parameter injection)
/// switch on this enum instead of comparing `transition_id` string literals
/// (arch rule transition_id_string_dispatch; the string id vocabulary lives
/// in `kLayerTransitionIds`).
enum class LayerTransitionKind : std::uint8_t {
    None,
    Crossfade,
    Slide,
    Zoom,
    WipeLinear,
    SmoothWipe,
    CircleIris,
    Flash,
    ProceduralRemotion,
    Remotion,
    Unknown, ///< Not a registered built-in id.
};

[[nodiscard]] inline constexpr LayerTransitionKind layer_transition_kind(
    std::string_view id) noexcept {
    if (id.empty() || id == "none") return LayerTransitionKind::None;
    if (id == "crossfade") return LayerTransitionKind::Crossfade;
    if (id == "slide") return LayerTransitionKind::Slide;
    if (id == "zoom") return LayerTransitionKind::Zoom;
    if (id == "wipe_linear") return LayerTransitionKind::WipeLinear;
    if (id == "smooth_wipe") return LayerTransitionKind::SmoothWipe;
    if (id == "circle_iris") return LayerTransitionKind::CircleIris;
    if (id == "flash") return LayerTransitionKind::Flash;
    if (id == "procedural_remotion") return LayerTransitionKind::ProceduralRemotion;
    if (id == "remotion") return LayerTransitionKind::Remotion;
    return LayerTransitionKind::Unknown;
}

}  // namespace chronon3d

#pragma once

#include <chronon3d/core/enum_utils.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/color.hpp>
#include <string>
#include <variant>

namespace chronon3d {

enum class TransitionDirection {
    None,
    Left,
    Right,
    Up,
    Down,
    In,
    Out
};

[[nodiscard]] inline TransitionDirection string_to_transition_direction(const std::string& str) {
    if (str == "left") return TransitionDirection::Left;
    if (str == "right") return TransitionDirection::Right;
    if (str == "up") return TransitionDirection::Up;
    if (str == "down") return TransitionDirection::Down;
    if (str == "in") return TransitionDirection::In;
    if (str == "out") return TransitionDirection::Out;
    return TransitionDirection::None;
}

[[nodiscard]] inline Easing string_to_easing(const std::string& str) {
    if (str == "linear") return Easing::Linear;
    if (str == "inquad" || str == "in_quad") return Easing::InQuad;
    if (str == "outquad" || str == "out_quad") return Easing::OutQuad;
    if (str == "inoutquad" || str == "in_out_quad") return Easing::InOutQuad;
    if (str == "incubic" || str == "in_cubic") return Easing::InCubic;
    if (str == "outcubic" || str == "out_cubic") return Easing::OutCubic;
    if (str == "inoutcubic" || str == "in_out_cubic") return Easing::InOutCubic;
    if (str == "inexpo" || str == "in_expo") return Easing::InExpo;
    if (str == "outexpo" || str == "out_expo") return Easing::OutExpo;
    if (str == "inoutexpo" || str == "in_out_expo") return Easing::InOutExpo;
    if (str == "insine" || str == "in_sine") return Easing::InSine;
    if (str == "outsine" || str == "out_sine") return Easing::OutSine;
    if (str == "inoutsine" || str == "in_out_sine") return Easing::InOutSine;
    if (str == "inback" || str == "in_back") return Easing::InBack;
    if (str == "outback" || str == "out_back") return Easing::OutBack;
    if (str == "inoutback" || str == "in_out_back") return Easing::InOutBack;
    if (str == "inelastic" || str == "in_elastic") return Easing::InElastic;
    if (str == "outelastic" || str == "out_elastic") return Easing::OutElastic;
    if (str == "inoutelastic" || str == "in_out_elastic") return Easing::InOutElastic;
    if (str == "inbounce" || str == "in_bounce") return Easing::InBounce;
    if (str == "outbounce" || str == "out_bounce") return Easing::OutBounce;
    if (str == "inoutbounce" || str == "in_out_bounce") return Easing::InOutBounce;
    if (str == "hold") return Easing::Hold;
    return Easing::Linear;
}

// Per-transition typed parameter structs.  The old hard-coded constants
// (feather, center, seed, speed, direction, colours) now live here as
// explicit fields, so every transition can be cached and reasoned about
// without reverse-engineering the renderer.
struct CrossfadeParams {};

struct SlideParams {
    /// Relative distance to travel, expressed as a fraction of the layer
    /// dimension along the chosen direction (1.0 = one full layer width/height).
    float distance = 1.0f;
};

struct WipeLinearParams {};

struct SmoothWipeParams {
    /// Width of the soft edge, relative to the smaller screen dimension.
    float feather = 0.1f;
};

struct CircleIrisParams {
    /// Normalised centre of the iris ([0,1] in each axis).
    Vec2 center{0.5f, 0.5f};
    /// Softness of the iris edge, relative to the iris radius.
    float feather = 0.1f;
};

struct FlashParams {
    /// Tint colour used for the flash burst (default white).
    Color color = Color::white();
};

struct ProceduralRemotionParams {
    /// Seed that drives the procedural noise pattern.
    float seed = 1.2f;
    /// Colour of the inner / mid burn.
    Color inner_color{1.0f, 0.45f, 0.0f, 1.0f};
    Color mid_color{1.0f, 0.45f, 0.0f, 1.0f};
    /// Colour of the outer hot leak.
    Color outer_color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct RemotionParams {
    /// Speed multiplier for the procedural reveal.
    float speed = 1.35f;
    /// Directional seed for the noise pattern.
    float direction = 3.0f;
    /// Hue rotation angle applied to the procedural colour.
    float angle = 0.0f;
};

using LayerTransitionParameters = std::variant<
    std::monostate,
    CrossfadeParams,
    SlideParams,
    WipeLinearParams,
    SmoothWipeParams,
    CircleIrisParams,
    FlashParams,
    ProceduralRemotionParams,
    RemotionParams
>;

/// Typed descriptor for a layer reveal / exit transition.
///
/// `transition_id` is the canonical string id used by the scene builder and
/// by serialisation; `parameters` is the typed, transition-specific data.
/// The catalog (`LayerTransitionCatalog`) is the only place that still
/// maps a string id to executable behaviour — new code must not add
/// `if (transition_id == ...)` dispatchers elsewhere.
struct LayerTransitionSpec {
    std::string transition_id{"none"};
    TransitionDirection direction{TransitionDirection::None};
    double duration{0.0}; // in seconds — 0 means no transition (static)
    double delay{0.0};
    Easing easing{Easing::Linear};
    LayerTransitionParameters parameters{};
};

} // namespace chronon3d

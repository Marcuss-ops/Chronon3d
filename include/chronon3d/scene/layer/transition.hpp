#pragma once

#include <chronon3d/core/types.hpp>
#include <chronon3d/animation/easing.hpp>
#include <string>

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

[[nodiscard]] inline std::string transition_direction_to_string(TransitionDirection dir) {
    switch (dir) {
        case TransitionDirection::Left: return "left";
        case TransitionDirection::Right: return "right";
        case TransitionDirection::Up: return "up";
        case TransitionDirection::Down: return "down";
        case TransitionDirection::In: return "in";
        case TransitionDirection::Out: return "out";
        default: return "none";
    }
}

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

struct LayerTransitionSpec {
    std::string transition_id{"none"};
    TransitionDirection direction{TransitionDirection::None};
    double duration{0.4}; // in seconds
    double delay{0.0};
    Easing easing{Easing::Linear};
};

} // namespace chronon3d

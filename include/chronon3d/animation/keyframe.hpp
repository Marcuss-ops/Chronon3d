#pragma once

#include <chronon3d/core/time.hpp>
#include <chronon3d/animation/easing.hpp>

namespace chronon3d {

template <typename T>
struct Keyframe {
    Frame frame{0};
    T value{};
    Easing easing{Easing::Linear};

    constexpr Keyframe() = default;
    constexpr Keyframe(Frame f, T v, Easing e = Easing::Linear) 
        : frame(f), value(v), easing(e) {}
    
    bool operator<(const Keyframe& other) const {
        return frame < other.frame;
    }
};

} // namespace chronon3d

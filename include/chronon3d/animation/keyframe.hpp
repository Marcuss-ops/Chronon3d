#pragma once

#include <chronon3d/core/frame.hpp>
#include <chronon3d/core/types.hpp>
#include <chronon3d/animation/easing.hpp>
#include <chronon3d/animation/interpolate.hpp>
#include <initializer_list>
#include <cstddef>

namespace chronon3d {

// Generic keyframe used by AnimatedValue<T>.
template <typename T>
struct Keyframe {
    Frame  frame{0};
    T      value{};
    Easing easing{Easing::Linear};

    constexpr Keyframe(Frame f, T v, Easing e = Easing::Linear)
        : frame(f), value(v), easing(e) {}

    // For std::sort / std::lower_bound
    bool operator<(const Keyframe& other) const { return frame < other.frame; }
    bool operator<(Frame f) const { return frame < f; }
};

// Convenience alias for scalar (f32) keyframes used with keyframes().
using KF = Keyframe<f32>;

// ---------------------------------------------------------------------------
// keyframes() — evaluate a scalar keyframe list at the given frame.
//
// Behaviour:
//   before first → holds first value
//   after  last  → holds last  value
//   between      → interpolates using Keyframe::easing of the earlier kf
//
// Usage:
//   auto x = keyframes(ctx.frame, {
//       KF{  0,   0.0f, Easing::OutCubic  },
//       KF{ 30, 400.0f, Easing::InOutCubic },
//       KF{ 60, 400.0f                     },   // hold (Linear = no movement)
//       KF{ 90, 800.0f }
//   });
// ---------------------------------------------------------------------------
inline f32 keyframes(Frame current, std::initializer_list<KF> kfs) {
    const std::size_t n = kfs.size();
    if (n == 0) return 0.0f;

    const KF* data = kfs.begin();

    if (current <= data[0].frame)    return data[0].value;
    if (current >= data[n-1].frame)  return data[n-1].value;

    for (std::size_t i = 0; i + 1 < n; ++i) {
        if (current >= data[i].frame && current < data[i+1].frame) {
            return interpolate(current,
                               data[i].frame,   data[i+1].frame,
                               data[i].value,   data[i+1].value,
                               data[i].easing);
        }
    }
    return data[n-1].value;
}

} // namespace chronon3d

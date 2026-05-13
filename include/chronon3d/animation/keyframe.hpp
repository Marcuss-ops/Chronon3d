#pragma once

#include <chronon3d/core/frame.hpp>
#include <chronon3d/core/types.hpp>
#include <chronon3d/animation/easing.hpp>
#include <chronon3d/animation/interpolate.hpp>
#include <initializer_list>
#include <cstddef>
#include <vector>

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

// Convenience alias for scalar (f32) keyframes.
using KF = Keyframe<f32>;

// Forward declarations for interpolation helpers.
template <typename T>
inline T interpolate_values(const T& a, const T& b, f32 t, Easing e) {
    if (e == Easing::Hold) return a;
    const f32 eased_t = easing::apply(e, t);
    return a + (b - a) * eased_t;
}

// ---------------------------------------------------------------------------
// KeyframeTrack<T> — manages a collection of typed keyframes.
// ---------------------------------------------------------------------------
template <typename T>
class KeyframeTrack {
public:
    KeyframeTrack(std::initializer_list<Keyframe<T>> kfs) : m_keyframes(kfs) {
    }

    // Float-based query — required for motion blur subframe accuracy.
    // Use ctx.effective_frame() as the argument when inside a composition.
    [[nodiscard]] T value_at_time(f32 current) const {
        if (m_keyframes.empty()) return T{};

        const auto* data = m_keyframes.data();
        const std::size_t n = m_keyframes.size();

        if (current <= static_cast<f32>(data[0].frame))     return data[0].value;
        if (current >= static_cast<f32>(data[n-1].frame))   return data[n-1].value;

        std::size_t lo = 0, hi = n - 1;
        while (lo + 1 < hi) {
            std::size_t mid = (lo + hi) / 2;
            if (static_cast<f32>(data[mid].frame) <= current) lo = mid;
            else hi = mid;
        }

        const auto& prev = data[lo];
        const auto& next = data[lo + 1];
        const f32 t = (current - static_cast<f32>(prev.frame))
                    / static_cast<f32>(next.frame - prev.frame);
        return interpolate_values(prev.value, next.value, t, prev.easing);
    }

    // Integer-frame aliases — delegate to value_at_time for consistency.
    [[nodiscard]] T value(Frame current) const { return value_at_time(static_cast<f32>(current)); }
    [[nodiscard]] T value_at(Frame current) const { return value_at_time(static_cast<f32>(current)); }

private:
    std::vector<Keyframe<T>> m_keyframes;
};

// ---------------------------------------------------------------------------
// keyframes<T>() — factory for KeyframeTrack<T>.
// Usage:
//   auto x = keyframes<f32>({ {0, 0.0f}, {60, 100.0f, Easing::OutCubic} }).value(frame);
// ---------------------------------------------------------------------------
template <typename T>
inline KeyframeTrack<T> keyframes(std::initializer_list<Keyframe<T>> kfs) {
    return KeyframeTrack<T>(kfs);
}

// ---------------------------------------------------------------------------
// Legacy support: keyframes(frame, {KF, KF, ...})
// ---------------------------------------------------------------------------
inline f32 keyframes(Frame current, std::initializer_list<KF> kfs) {
    return keyframes<f32>(kfs).value(current);
}

} // namespace chronon3d

#pragma once

#include <chronon3d/animation/easing.hpp>
#include <chronon3d/animation/interpolate.hpp>
#include <chronon3d/core/frame.hpp>
#include <chronon3d/core/types.hpp>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <utility>
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
    KeyframeTrack() = default;

    KeyframeTrack(std::initializer_list<Keyframe<T>> kfs) {
        for (const auto& kf : kfs) {
            key(kf.frame, kf.value, kf.easing);
        }
    }

    KeyframeTrack& key(Frame frame, const T& value, Easing easing = Easing::Linear) {
        m_keyframes.emplace_back(frame, value, easing);
        m_sorted = false;
        return *this;
    }

    KeyframeTrack& key(Frame frame, T&& value, Easing easing = Easing::Linear) {
        m_keyframes.emplace_back(frame, std::move(value), easing);
        m_sorted = false;
        return *this;
    }

    [[nodiscard]] bool empty() const { return m_keyframes.empty(); }
    [[nodiscard]] std::size_t size() const { return m_keyframes.size(); }

    void clear() {
        m_keyframes.clear();
        m_sorted = true;
    }

    [[nodiscard]] T sample_at(f32 current) const {
        if (m_keyframes.empty()) {
            return T{};
        }

        ensure_sorted();

        const auto it = std::upper_bound(
            m_keyframes.begin(),
            m_keyframes.end(),
            current,
            [](f32 value, const Keyframe<T>& kf) {
                return value < static_cast<f32>(kf.frame);
            }
        );

        if (it == m_keyframes.begin()) {
            return it->value;
        }

        if (it == m_keyframes.end()) {
            return (it - 1)->value;
        }

        const auto prev = it - 1;
        const auto& next = *it;

        if (next.frame == prev->frame) {
            return next.value;
        }

        const f32 t = (current - static_cast<f32>(prev->frame))
                     / static_cast<f32>(next.frame - prev->frame);
        return interpolate_values(prev->value, next.value, t, prev->easing);
    }

    [[nodiscard]] T sample(Frame current) const {
        return sample_at(static_cast<f32>(current));
    }

    // Float-based query — required for motion blur subframe accuracy.
    // Use ctx.effective_frame() as the argument when inside a composition.
    [[nodiscard]] T value_at_time(f32 current) const { return sample_at(current); }

    // Integer-frame aliases — delegate to sample for consistency.
    [[nodiscard]] T value(Frame current) const { return sample(current); }
    [[nodiscard]] T value_at(Frame current) const { return sample(current); }

    [[nodiscard]] T operator()(Frame current) const { return sample(current); }

private:
    void ensure_sorted() const {
        if (m_sorted) {
            return;
        }

        std::stable_sort(m_keyframes.begin(), m_keyframes.end(),
            [](const Keyframe<T>& a, const Keyframe<T>& b) {
                return a.frame < b.frame;
            });
        m_sorted = true;
    }

    mutable std::vector<Keyframe<T>> m_keyframes;
    mutable bool m_sorted{true};
};

// ---------------------------------------------------------------------------
// keyframes<T>() — factory for KeyframeTrack<T>.
// Usage:
//   auto x = keyframes<f32>({ {0, 0.0f}, {60, 100.0f, Easing::OutCubic} }).sample(frame);
// ---------------------------------------------------------------------------
template <typename T>
inline KeyframeTrack<T> keyframes(std::initializer_list<Keyframe<T>> kfs) {
    return KeyframeTrack<T>(kfs);
}

// ---------------------------------------------------------------------------
// Legacy support: keyframes(frame, {KF, KF, ...})
// ---------------------------------------------------------------------------
inline f32 keyframes(Frame current, std::initializer_list<KF> kfs) {
    return keyframes<f32>(kfs).sample(current);
}

} // namespace chronon3d

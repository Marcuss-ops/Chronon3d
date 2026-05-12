#pragma once

#include <chronon3d/animation/keyframe.hpp>
#include <vector>
#include <algorithm>

namespace chronon3d {

// Generic component-wise lerp used by AnimatedValue<T>.
template <typename T>
inline T lerp(const T& a, const T& b, f32 t) {
    return a + (b - a) * t;
}

template <typename T>
class AnimatedValue {
public:
    AnimatedValue() = default;
    explicit AnimatedValue(T default_value) : m_default_value(default_value) {}

    // Add a keyframe; keys are kept sorted by frame.
    void add_keyframe(Frame frame, const T& value, Easing easing = Easing::Linear) {
        m_keyframes.emplace_back(frame, value, easing);
        std::sort(m_keyframes.begin(), m_keyframes.end());
    }

    // Fluent alias for add_keyframe — enables chaining.
    AnimatedValue& key(Frame frame, const T& value, Easing easing = Easing::Linear) {
        add_keyframe(frame, value, easing);
        return *this;
    }

    // Set a constant value (clears all keyframes).
    AnimatedValue& set(const T& value) {
        clear();
        m_default_value = value;
        return *this;
    }

    // Preferred name in new code.
    [[nodiscard]] T value_at(Frame frame) const { return evaluate(frame); }

    [[nodiscard]] T evaluate(Frame frame) const {
        if (m_keyframes.empty()) {
            return m_default_value;
        }

        if (frame <= m_keyframes.front().frame) {
            return m_keyframes.front().value;
        }

        if (frame >= m_keyframes.back().frame) {
            return m_keyframes.back().value;
        }

        auto it   = std::lower_bound(m_keyframes.begin(), m_keyframes.end(), Keyframe<T>{frame, T{}});
        auto next = it;
        auto prev = std::prev(it);

        f32 t       = static_cast<f32>(frame - prev->frame) / static_cast<f32>(next->frame - prev->frame);
        f32 eased_t = easing::apply(prev->easing, t);

        return lerp(prev->value, next->value, eased_t);
    }

    [[nodiscard]] bool is_animated() const { return !m_keyframes.empty(); }
    void clear() { m_keyframes.clear(); }

private:
    T m_default_value{};
    std::vector<Keyframe<T>> m_keyframes;
};

} // namespace chronon3d

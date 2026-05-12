#pragma once

#include <chronon3d/animation/keyframe.hpp>
#include <vector>
#include <algorithm>

namespace chronon3d {

// Helper for interpolation
template <typename T>
inline T lerp(const T& a, const T& b, f32 t) {
    return a + (b - a) * t;
}

template <typename T>
class AnimatedValue {
public:
    AnimatedValue() = default;
    explicit AnimatedValue(T default_value) : m_default_value(default_value) {}

    void add_keyframe(Frame frame, const T& value, Easing easing = Easing::Linear) {
        m_keyframes.emplace_back(frame, value, easing);
        std::sort(m_keyframes.begin(), m_keyframes.end());
    }

    void set(const T& value) {
        clear();
        m_default_value = value;
    }

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

        // Find the interval
        auto it = std::lower_bound(m_keyframes.begin(), m_keyframes.end(), Keyframe<T>{frame, T{}});
        auto next = it;
        auto prev = std::prev(it);

        f32 t = static_cast<f32>(frame - prev->frame) / static_cast<f32>(next->frame - prev->frame);
        f32 eased_t = easing::apply(t, prev->easing);

        return lerp(prev->value, next->value, eased_t);
    }

    [[nodiscard]] bool is_animated() const { return !m_keyframes.empty(); }
    void clear() { m_keyframes.clear(); }

private:
    T m_default_value{};
    std::vector<Keyframe<T>> m_keyframes;
};

} // namespace chronon3d

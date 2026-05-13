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

enum class LoopMode {
    Hold,
    Loop,
    PingPong
};

struct Wiggle {
    f32 freq{1.5f};
    f32 amp{2.0f};
    u32 seed{0};

    [[nodiscard]] f32 eval(f32 t, f32 base = 0.0f) const {
        const f32 n = std::sin(t * freq * 6.283f + static_cast<f32>(seed)) * 0.5f
                    + std::sin(t * freq * 12.7f + static_cast<f32>(seed) * 1.3f) * 0.3f;
        return base + n * amp;
    }
};

template <typename T>

class AnimatedValue {
public:
    AnimatedValue() = default;
    explicit AnimatedValue(T default_value) : m_default_value(default_value) {}

    AnimatedValue& loop_mode(LoopMode mode) {
        m_loop_mode = mode;
        return *this;
    }

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

        const Frame start_f = m_keyframes.front().frame;
        const Frame end_f   = m_keyframes.back().frame;
        const Frame range   = end_f - start_f;

        Frame eval_frame = frame;

        if (m_loop_mode == LoopMode::Loop && range > 0) {
            if (frame < start_f) {
                // To properly loop negative frames: offset to positive
                eval_frame = end_f - (start_f - frame - 1) % range - 1;
            } else {
                eval_frame = start_f + (frame - start_f) % range;
            }
        } else if (m_loop_mode == LoopMode::PingPong && range > 0) {
            Frame t = (frame < start_f) ? (start_f - frame) : (frame - start_f);
            Frame cycle = t / range;
            Frame offset = t % range;
            if (cycle % 2 == 0) {
                eval_frame = start_f + offset;
            } else {
                eval_frame = end_f - offset;
            }
        } else {
            // Hold mode (default)
            if (frame <= start_f) return m_keyframes.front().value;
            if (frame >= end_f)   return m_keyframes.back().value;
        }

        auto it   = std::lower_bound(m_keyframes.begin(), m_keyframes.end(), eval_frame,
            [](const Keyframe<T>& kf, Frame f) { return kf.frame < f; });
        
        if (it == m_keyframes.begin()) return it->value;
        if (eval_frame == it->frame) return it->value;

        auto next = it;
        auto prev = std::prev(it);

        const f32 t = static_cast<f32>(eval_frame - prev->frame) / static_cast<f32>(next->frame - prev->frame);
        return interpolate_values(prev->value, next->value, t, prev->easing);
    }

    [[nodiscard]] bool is_animated() const { return !m_keyframes.empty(); }
    void clear() { m_keyframes.clear(); }

private:
    T m_default_value{};
    std::vector<Keyframe<T>> m_keyframes;
    LoopMode m_loop_mode{LoopMode::Hold};
};

} // namespace chronon3d

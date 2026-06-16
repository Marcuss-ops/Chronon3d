#pragma once

#include <chronon3d/animation/core/keyframe.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <glm/gtx/quaternion.hpp>
#include <vector>
#include <algorithm>

namespace chronon3d {

/// AnimatedQuaternion — a dedicated Quaternion animation track using
/// shortest-path SLERP interpolation.
///
/// Quaternions require specialised interpolation because:
///   - Linear interpolation (lerp) does not preserve unit length.
///   - GLM's slerp can take the long way around the unit sphere when
///     the dot product of two quaternions is negative.
///   - Euler→Quat→Euler round-trips can introduce ±180° discontinuities.
///
/// This class addresses all three: it uses SLERP with automatic sign
/// correction (negate quaternion when dot < 0), always normalises the
/// result, and discourages Euler access.
///
/// Usage:
///   AnimatedQuaternion orient;
///   orient.key(0,   Quat{1,0,0,0});
///   orient.key(60,  glm::angleAxis(glm::radians(90.0f), Vec3{0,1,0}));
///   Quat q = orient.evaluate(SampleTime::from_frame(30, 30.0));
class AnimatedQuaternion {
public:
    AnimatedQuaternion() = default;

    explicit AnimatedQuaternion(Quat default_value)
        : m_default_value(glm::normalize(default_value)) {}

    /// Add a keyframe at `frame` with the given quaternion value.
    /// The value is normalised on insertion.
    AnimatedQuaternion& key(Frame frame, Quat value,
                            EasingCurve easing = EasingCurve{Easing::Linear}) {
        m_keyframes.emplace_back(frame, glm::normalize(value), easing, false);
        std::sort(m_keyframes.begin(), m_keyframes.end());
        return *this;
    }

    /// Set a constant quaternion (clears all keyframes).
    AnimatedQuaternion& set(const Quat& value) {
        clear();
        m_default_value = glm::normalize(value);
        return *this;
    }

    /// Clear all keyframes.
    void clear() { m_keyframes.clear(); }

    /// Evaluate at a sub-frame time via SLERP.
    [[nodiscard]] Quat evaluate(SampleTime time) const {
        return evaluate_base_double(time.frame);
    }

    /// Evaluate at an integer frame.
    [[nodiscard]] Quat evaluate(Frame frame) const {
        return evaluate(SampleTime::from_frame_int(frame, FrameRate{30, 1}));
    }

    /// True if any keyframes are present.
    [[nodiscard]] bool is_animated() const { return !m_keyframes.empty(); }

    /// True if there are any keyframes (camera cache needs this).
    [[nodiscard]] bool is_time_dependent() const { return is_animated(); }

    /// Returns the frame of the last keyframe, or Frame{0} if empty.
    [[nodiscard]] Frame last_keyframe_time() const {
        if (m_keyframes.empty()) return Frame{0};
        return m_keyframes.back().frame;
    }

private:
    [[nodiscard]] Quat evaluate_base_double(double frame) const {
        if (m_keyframes.empty()) return m_default_value;

        const double start_f = static_cast<double>(m_keyframes.front().frame);
        const double end_f   = static_cast<double>(m_keyframes.back().frame);

        if (frame <= start_f) return m_keyframes.front().value;
        if (frame >= end_f)   return m_keyframes.back().value;

        // Binary search
        size_t idx = 0;
        for (size_t i = 0; i + 1 < m_keyframes.size(); ++i) {
            if (static_cast<double>(m_keyframes[i + 1].frame) >= frame) {
                idx = i;
                break;
            }
            idx = i + 1;
        }
        if (idx + 1 >= m_keyframes.size()) return m_keyframes.back().value;

        const auto& prev = m_keyframes[idx];
        const auto& next = m_keyframes[idx + 1];

        const double prev_f = static_cast<double>(prev.frame);
        const double next_f = static_cast<double>(next.frame);
        if (next_f <= prev_f) return prev.value;

        const f32 t = static_cast<f32>((frame - prev_f) / (next_f - prev_f));
        const f32 eased_t = prev.easing.apply(std::clamp(t, 0.0f, 1.0f));

        // Shortest-path SLERP with sign correction
        Quat a = glm::normalize(prev.value);
        Quat b = glm::normalize(next.value);

        // If dot < 0, negate b to take the short path on the unit sphere
        if (glm::dot(a, b) < 0.0f) b = -b;

        return glm::normalize(glm::slerp(a, b, eased_t));
    }

    Quat m_default_value{1.0f, 0.0f, 0.0f, 0.0f};
    mutable std::vector<Keyframe<Quat>> m_keyframes;
};

} // namespace chronon3d

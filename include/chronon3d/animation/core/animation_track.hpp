// ── AnimationTrack<T> — declarative typed animation curve builder ─────────
//
// Mirrors the .from(frame, value, easing).to(frame, value, easing) builder
// style of Framer Motion and React-Spring, plus Remotion-style named keys.
// Built on top of the existing Keyframe<T>; designers write the curve once
// at composition construction time, then apply it to any number of animated
// properties with AnimatedValue<T>::apply_track() (or animate_x() when the
// scalar drives a single axis of a Vec3).
//
// Why a separate type from KeyframeTrack<T>:
//   KeyframeTrack<T> is the RUNTIME evaluator (sample_at, m_sorted cache,
//   binary search). AnimationTrack<T> is the BUILD-TIME declarative builder;
//   it just owns an ordered vector of Keyframe<T>, sorted by Frame, and
//   exposes them for baking into an AnimatedValue<T>'s existing
//   evaluation pipeline. Zero runtime cost — emit() is inlined at the
//   call site.
//
// Example:
//
//     auto x_perspective = AnimationTrack<float>()
//         .from(Frame{0},   0.766f, Easing::OutCubic)
//         .to(Frame{100},   0.951f, Easing::Linear)
//         .to(Frame{150},   0.990f, Easing::Linear);
//
//     // Reuse across multiple layers:
//     title.scale_anim().animate_x(x_perspective);
//     title_shadow.scale_anim().animate_x(x_perspective);
//
#pragma once

#include <chronon3d/animation/core/keyframe.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chronon3d {

/// Deterministic scalar driver compiled from an authored animation-track
/// driver. It is stored on AnimatedValue<float> and evaluated from sample
/// frame/time only; it never reads a clock or global RNG.
struct ScalarAnimationDriver {
    enum class Kind : std::uint8_t { Oscillate, RandomHold, PosterizeTime };
    enum class Waveform : std::uint8_t { Sine, Triangle, Sawtooth, Square };
    Kind kind{Kind::Oscillate};
    Waveform waveform{Waveform::Sine};
    float frequency_hz{1.0f};
    float amplitude{1.0f};
    float phase_deg{0.0f};
    float offset{0.0f};
    std::uint32_t interval_frames{1};
    float min{0.0f};
    float max{1.0f};
    std::uint32_t seed{0};
    float fps{8.0f};
};

template <typename T>
class AnimationTrack {
public:
    AnimationTrack() = default;

    /// Add the first/starting keyframe. Repeated calls just append — keys
    /// are kept sorted by Frame so callers can write them in either
    /// chronological or narrative order.
    AnimationTrack& from(Frame frame, const T& value, Easing easing = Easing::Linear) {
        m_keys.emplace_back(frame, value, EasingCurve{easing});
        return *this;
    }

    /// Add a follow-on keyframe.
    AnimationTrack& to(Frame frame, const T& value, Easing easing = Easing::Linear) {
        m_keys.emplace_back(frame, value, EasingCurve{easing});
        return *this;
    }

    /// Alias for `to` that reads more naturally when naming the curve points
    /// with semantic anchors (e.g. ".at(Frame{75}, 1.0f, Easing::InOutSine)").
    AnimationTrack& at(Frame frame, const T& value, Easing easing = Easing::Linear) {
        return to(frame, value, easing);
    }

    /// Read-only view of the underlying keyframes. AnimatedValue<T>::apply_track
    /// consumes this to bake into its own evaluation pipeline.
    [[nodiscard]] const std::vector<Keyframe<T>>& keys() const noexcept { return m_keys; }

    [[nodiscard]] bool empty() const noexcept { return m_keys.empty(); }

    [[nodiscard]] std::size_t size() const noexcept { return m_keys.size(); }

    void clear() noexcept { m_keys.clear(); }

private:
    std::vector<Keyframe<T>> m_keys;
};

} // namespace chronon3d

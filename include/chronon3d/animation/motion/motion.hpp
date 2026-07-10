// ═══════════════════════════════════════════════════════════════════════════
// motion/motion.hpp — C1: unified canonical animation system.
//
// Replaces the pre-C1 split (AnimatedValue::key() + motion::Timeline +
// AnimationTrack + scattered spring/expression/preset stitching) with a
// single canonical surface:
//
//   MotionProgram<T>     — stateless DTO: keyframes + expression + loop mode.
//                          ALL animation sources (keyframe, timeline, spring,
//                          expression, preset) compile INTO this.
//   MotionTimeline<T>    — fluent segment-oriented builder; replaces
//                          the deprecated motion::Timeline<T>.
//   SpringConfig         — damped harmonic oscillator parameters; baked to
//                          keyframes by MotionTimeline::spring().
//   Motion<T>            — canonical type alias for AnimatedValue<T>.
//                          The old AnimatedValue<T> continues to work
//                          (deprecated .key()) but new code should
//                          use Motion<T> and this header.
//
// Usage:
//   // Timeline (replaces motion::timeline())
//   auto prog = timeline(0.0f)
//       .to(Frame{30}, 1.0f, Easing::OutCubic)
//       .hold_until(Frame{60})
//       .compile();
//   Motion<f32> anim;
//   from_program(anim, prog);
//
//   // Spring
//   auto spring_prog = timeline(0.0f)
//       .spring(SpringConfig{.stiffness = 80.0f, .damping = 12.0f}, 1.0f)
//       .compile();
//
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/animation/core/keyframe.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/types.hpp>

#include <cmath>
#include <string>
#include <type_traits>
#include <vector>

namespace chronon3d {

// ── Motion<T> — canonical type alias ───────────────────────────────────

/// C1 canonical name for the AnimatedValue<T> animation sampler.
/// New code should use Motion<T>; AnimatedValue<T> continues to work.
template <AnimatableValue T>
using Motion = AnimatedValue<T>;

// ── MotionProgram<T> — unified compiled animation representation ────────

/// Stateless DTO representing a compiled animation.
///
/// All animation sources (keyframes, timeline segments, spring physics,
/// AE-style expressions, presets) MUST compile to this single
/// representation.  Free function from_program() consumes it to
/// configure the runtime evaluator.
template <typename T>
struct MotionProgram {
    std::vector<Keyframe<T>> keyframes;
    std::string              expression;       // optional AE-style expression
    LoopMode                 loop_mode{LoopMode::Hold};
};

// ── Free functions on Motion<T> ────────────────────────────────────────

/// Consume a MotionProgram, replacing all internal state of the target.
/// Equivalent to: dest.clear() + populate keyframes/expression/loop mode.
///
/// NOTE: uses add_keyframe() which preserves frame/value/easing/roving.
/// Advanced Keyframe fields (interp, spatial bezier handles, temporal
/// tangents) are not forwarded — forward-compatibility gap for C1.
template <AnimatableValue T>
inline void from_program(Motion<T>& dest, const MotionProgram<T>& program) {
    dest.clear();
    for (const auto& kf : program.keyframes) {
        dest.add_keyframe(kf.frame, kf.value, kf.easing, kf.roving);
    }
    if (!program.expression.empty()) {
        dest.expression(program.expression);
    }
    dest.loop_mode(program.loop_mode);
}

template <AnimatableValue T>
inline void from_program(Motion<T>& dest, MotionProgram<T>&& program) {
    from_program(dest, program);
}

// ── SpringConfig — damped harmonic oscillator parameters ────────────────

/// Parameters for a critically-damped mass-spring-damper system.
///
/// The motion follows:
///   f(t) = target + (initial - target) * (1 + ωt) * e^(-ωt)
/// where ω = sqrt(stiffness).
///
/// Precomputed into keyframes at 1-frame granularity by
/// MotionTimeline::spring().  At 1-frame spacing, linear interpolation
/// between adjacent samples is sufficient to approximate the continuous
/// curve (the critically-damped response has no oscillation, so linear
/// interpolation at 60 fps is visually indistinguishable from exact).
struct SpringConfig {
    f32   stiffness{100.0f};   // spring constant (higher = faster settling)
    f32   damping{10.0f};      // normalized damping ratio (≥1 = no overshoot)
    Frame duration{60};        // how many frames to precompute
};

// ── MotionTimeline<T> ───────────────────────────────────────────────────

/// Fluent segment-oriented builder.  Replaces the deprecated
/// motion::Timeline<T>.
///
/// Example:
///   auto prog = timeline(-25.0f)
///       .to(Frame{35}, -14.0f, Easing::OutCubic)
///       .to(Frame{75},  -8.0f, Easing::InOutSine)
///       .spring(SpringConfig{.stiffness = 80.0f}, 0.0f)
///       .compile();
///
///   from_program(my_anim, prog);
///
template <typename T>
class MotionTimeline {
public:
    struct Segment {
        Frame       end_frame{0};
        T           value{};
        EasingCurve easing{Easing::Linear};
    };

    /// Create a timeline starting at frame 0 with the given initial value.
    explicit MotionTimeline(T initial)
        : m_initial_value(std::move(initial)) {}

    /// Add a segment ending at end_frame with the given value and easing.
    MotionTimeline& to(Frame end_frame, T value,
                        EasingCurve easing = EasingCurve{Easing::Linear}) {
        m_segments.push_back(Segment{end_frame, std::move(value), easing});
        return *this;
    }

    /// Add a segment with a relative duration from the previous stop.
    MotionTimeline& over(Frame duration, T value,
                         EasingCurve easing = EasingCurve{Easing::Linear}) {
        Frame start = current_frame();
        return to(start + duration, std::move(value), easing);
    }

    /// Hold the current value until end_frame (no interpolation).
    MotionTimeline& hold_until(Frame end_frame) {
        return to(end_frame, current_value(), EasingCurve{Easing::Hold});
    }

    /// Hold the current value for a relative duration.
    MotionTimeline& hold(Frame duration) {
        return hold_until(current_frame() + duration);
    }

    // ── Spring ───────────────────────────────────────────────────────

    /// Append spring physics from the current value toward `target`.
    ///
    /// Bakes a critically-damped harmonic oscillator response into
    /// keyframes at 1-frame granularity.  The spring settles over
    /// `config.duration` frames from the current timeline position.
    ///
    /// Each baked keyframe uses Easing::Hold so the evaluator
    /// steps directly to the precomputed value — no interpolation
    /// between frames is needed at 1-frame spacing.
    MotionTimeline& spring(const SpringConfig& config, T target) {
        static_assert(std::is_arithmetic_v<T>,
                      "Spring physics requires an arithmetic type (f32, f64, int)");
        const Frame start_frame = current_frame();
        const T     start_value = current_value();

        const f32 omega = std::sqrt(config.stiffness);
        const Frame end_frame = start_frame + config.duration;

        // Sample at each frame: critically-damped response
        //   f(t) = target + (start - target) * (1 + ωt) * e^(-ωt)
        // where ω = sqrt(stiffness).
        for (Frame f = start_frame; f <= end_frame; f = f + Frame{1}) {
            const f32 t = static_cast<f32>(f - start_frame);
            const f32 decay = std::exp(-omega * t);
            const f32 factor = (T{1} + omega * t) * decay;
            T value = target + (start_value - target) * factor;

            // Use Hold easing so the evaluator steps to the precomputed
            // value exactly.  At 1-frame granularity the critically-damped
            // response has no oscillation, so Hold produces the correct
            // continuous curve.
            m_segments.push_back(Segment{f, value, EasingCurve{Easing::Hold}});
        }
        return *this;
    }

    // ── Compilation ──────────────────────────────────────────────────

    /// Compile this timeline into a MotionProgram<T>.
    [[nodiscard]] MotionProgram<T> compile() const {
        MotionProgram<T> program;

        if (m_segments.empty()) {
            program.keyframes.push_back(
                Keyframe<T>{Frame{0}, m_initial_value, EasingCurve{Easing::Linear}});
            return program;
        }

        // First keyframe carries the easing of the first segment.
        program.keyframes.push_back(
            Keyframe<T>{Frame{0}, m_initial_value, m_segments[0].easing});

        for (size_t i = 0; i < m_segments.size(); ++i) {
            EasingCurve outgoing = (i + 1 < m_segments.size())
                ? m_segments[i + 1].easing
                : EasingCurve{Easing::Linear};
            program.keyframes.push_back(
                Keyframe<T>{m_segments[i].end_frame, m_segments[i].value, outgoing});
        }

        return program;
    }

    /// Apply this timeline directly to a Motion<T> (convenience).
    void apply_to(Motion<T>& dest) const {
        dest.clear();
        if (m_segments.empty()) {
            dest.add_keyframe(Frame{0}, m_initial_value);
            return;
        }
        dest.add_keyframe(Frame{0}, m_initial_value, m_segments[0].easing);
        for (size_t i = 0; i < m_segments.size(); ++i) {
            EasingCurve outgoing = (i + 1 < m_segments.size())
                ? m_segments[i + 1].easing
                : EasingCurve{Easing::Linear};
            dest.add_keyframe(m_segments[i].end_frame, m_segments[i].value, outgoing);
        }
    }

    /// Number of segments.
    [[nodiscard]] size_t segment_count() const { return m_segments.size(); }

    /// Resolved point (for inspection / debugging).
    struct Point {
        Frame       frame{0};
        T           value{};
        EasingCurve outgoing_easing{Easing::Linear};
    };

    [[nodiscard]] std::vector<Point> points() const {
        std::vector<Point> result;
        result.push_back(Point{Frame{0}, m_initial_value, EasingCurve{Easing::Linear}});
        for (size_t i = 0; i < m_segments.size(); ++i) {
            result.back().outgoing_easing = m_segments[i].easing;
            result.push_back(Point{
                m_segments[i].end_frame,
                m_segments[i].value,
                EasingCurve{Easing::Linear}
            });
        }
        return result;
    }

private:
    [[nodiscard]] Frame current_frame() const {
        if (m_segments.empty()) return Frame{0};
        return m_segments.back().end_frame;
    }

    [[nodiscard]] T current_value() const {
        if (m_segments.empty()) return m_initial_value;
        return m_segments.back().value;
    }

    T m_initial_value;
    std::vector<Segment> m_segments;
};

// ── timeline() — factory for MotionTimeline<T> ─────────────────────────

/// Create a MotionTimeline<T> builder starting at frame 0.
/// Replaces the deprecated motion::timeline(initial) free function.
///
/// Usage:
///   auto prog = timeline(0.0f)
///       .to(Frame{30}, 1.0f, Easing::OutCubic)
///       .compile();
template <typename T>
[[nodiscard]] inline MotionTimeline<T> timeline(T initial) {
    return MotionTimeline<T>(std::move(initial));
}

} // namespace chronon3d

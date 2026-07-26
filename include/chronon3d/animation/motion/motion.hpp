// ═══════════════════════════════════════════════════════════════════════════
// motion/motion.hpp — C1: unified canonical animation system.
//
// Replaces the pre-C1 split (AnimatedValue::key() + AnimationTrack +
// scattered spring/expression/preset stitching) with a
// single canonical surface:
//
//   MotionProgram<T>     — stateless DTO: keyframes + expression + loop mode.
//                          ALL animation sources (keyframe, timeline, spring,
//                          expression, preset) compile INTO this.
//   MotionTimeline<T>    — fluent segment-oriented builder; replaces
//                          the legacy timeline API. Uses canonical
//                          chronon3d::SpringConfig + sample_spring()
//                          (defined in animation/easing/spring.hpp) for
//                          spring physics — single source of truth
//                          (TICKET-ANIM-SPRING-UNIFY).
//   Motion<T>            — canonical type alias for AnimatedValue<T>.
//                          The old AnimatedValue<T> continues to work
//                          (deprecated .key()) but new code should
//                          use Motion<T> and this header.
//
// Usage:
//   // Timeline builder
//   auto prog = timeline(0.0f)
//       .to(Frame{30}, 1.0f, Easing::OutCubic)
//       .hold_until(Frame{60})
//       .compile();
//   Motion<f32> anim;
//   from_program(anim, prog);
//
//   // Spring (canonical SpringConfig from animation/easing/spring.hpp)
//   auto spring_prog = timeline(0.0f)
//       .spring(Frame{60}, 1.0f,
//               SpringConfig{.stiffness = 80.0f, .damping = 12.0f})
//       .compile();
//
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/animation/core/keyframe.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/easing/spring.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/time.hpp>
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

// ── SpringConfig — canonical damped harmonic oscillator parameters ───────
// DELETED: motion-local SpringConfig was removed (TICKET-ANIM-SPRING-UNIFY,
// Cat-3 anti-dup). Use chronon3d::SpringConfig from
// animation/easing/spring.hpp — the unique canonical source of truth.
// Spring physics are sampled via chronon3d::sample_spring(TimeSeconds, ...),
// which is the single closed-form function for the damped harmonic oscillator
// with initial velocity support.

/// Canonical bake framerate used by MotionTimeline::spring() to convert
/// frame deltas to TimeSeconds when precomputing keyframes.
/// 60 fps matches the canvas output canonical.
inline constexpr f32 kSpringBakeFps = 60.0f;

// ── MotionTimeline<T> ───────────────────────────────────────────────────

/// Fluent segment-oriented builder.  Replaces the deprecated
// legacy timeline API.
///
/// Example:
///   auto prog = timeline(-25.0f)
///       .to(Frame{35}, -14.0f, Easing::OutCubic)
///       .to(Frame{75},  -8.0f, Easing::InOutSine)
///       .spring(Frame{60}, 0.0f, SpringConfig{.stiffness = 80.0f})
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
    /// Bakes sample_spring(TimeSeconds, ...) into keyframes at 1-frame
    /// granularity over `duration` frames (sampled at kSpringBakeFps).
    /// Delegates to the canonical closed-form function — the single
    /// source of truth for damped harmonic oscillator evaluation
    /// (TICKET-ANIM-SPRING-UNIFY).
    ///
    /// Each baked keyframe uses Easing::Hold so the evaluator steps
    /// directly to the precomputed value.
    MotionTimeline& spring(Frame duration, T target,
                           const SpringConfig& config = {}) {
        static_assert(std::is_arithmetic_v<T>,
                      "Spring physics requires an arithmetic type (f32, f64, int)");
        const Frame start_frame = current_frame();
        const T     start_value = current_value();
        const Frame end_frame   = start_frame + duration;

        const f32 from_f = static_cast<f32>(start_value);
        const f32 to_f   = static_cast<f32>(target);

        for (Frame f = start_frame; f <= end_frame; f = f + Frame{1}) {
            const TimeSeconds t = static_cast<TimeSeconds>(f - start_frame)
                                  / static_cast<TimeSeconds>(kSpringBakeFps);
            const f32 value = sample_spring(t, from_f, to_f, config);
            m_segments.push_back(
                Segment{f, static_cast<T>(value), EasingCurve{Easing::Hold}});
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
/// Factory for MotionTimeline<T>.
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

#pragma once

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <vector>
#include <algorithm>

namespace chronon3d::motion {

// ─────────────────────────────────────────────────────────────────────────────
// motion::Timeline<T>
//
// A fluent, segment-oriented animation builder that generates keyframes for
// AnimatedValue<T>.  Unlike the raw AnimatedValue::key() API (where the easing
// on a keyframe controls the *next* segment), Timeline expresses easing
// naturally: the easing on a segment describes how the value arrives at its
// endpoint.
//
// Usage:
//   motion::timeline(-25.0f)
//       .to(Frame{35}, -14.0f, Easing::OutCubic)
//       .to(Frame{75},  -8.0f, Easing::InOutSine)
//       .hold_until(Frame{140})
//       .to(Frame{150}, -10.0f, Easing::InSine)
//       .apply_to(my_animated_value);
//
// Internally the easing is mapped onto the preceding keyframe so that
// AnimatedValue interpolates correctly.
// ─────────────────────────────────────────────────────────────────────────────

template <typename T>
class Timeline {
public:
    struct Segment {
        Frame end_frame{0};
        T     value{};
        EasingCurve easing{Easing::Linear};
    };

    /// Create a timeline starting at frame 0 with the given initial value.
    explicit Timeline(T initial)
        : m_initial_value(std::move(initial)) {}

    /// Add a segment ending at end_frame with the given value and easing.
    /// The easing describes how the value arrives at end_frame from the
    /// previous stop.
    Timeline& to(Frame end_frame, T value, EasingCurve easing = EasingCurve{Easing::Linear}) {
        m_segments.push_back(Segment{end_frame, std::move(value), easing});
        return *this;
    }

    /// Add a segment with a relative duration from the previous stop.
    Timeline& over(Frame duration, T value, EasingCurve easing = EasingCurve{Easing::Linear}) {
        Frame start = current_frame();
        return to(start + duration, std::move(value), easing);
    }

    /// Hold the current value until end_frame (no interpolation needed).
    Timeline& hold_until(Frame end_frame) {
        return to(end_frame, current_value());
    }

    /// Hold the current value for a relative duration.
    Timeline& hold(Frame duration) {
        return hold_until(current_frame() + duration);
    }

    /// Convert this timeline into AnimatedValue keyframes.
    /// The easing on each segment is set on the *preceding* keyframe
    /// because AnimatedValue interpolates using prev->easing.
    void apply_to(AnimatedValue<T>& dest) const {
        dest.clear();

        if (m_segments.empty()) {
            dest.key(Frame{0}, m_initial_value);
            return;
        }

        // First keyframe carries the easing of the first segment.
        dest.key(Frame{0}, m_initial_value, m_segments[0].easing);

        for (size_t i = 0; i < m_segments.size(); ++i) {
            // The easing on this keyframe controls the NEXT segment (i+1),
            // or Linear if this is the last one.
            EasingCurve outgoing = (i + 1 < m_segments.size())
                ? m_segments[i + 1].easing
                : EasingCurve{Easing::Linear};
            dest.key(m_segments[i].end_frame, m_segments[i].value, outgoing);
        }
    }

    /// Number of segments.
    [[nodiscard]] size_t segment_count() const { return m_segments.size(); }

    /// Resolved point: a frame, value, and the easing that controls the
    /// segment *outgoing* from this point toward the next one (matching
    /// AnimatedValue's `prev->easing` convention).
    struct Point {
        Frame frame{0};
        T     value{};
        EasingCurve outgoing_easing{Easing::Linear};
    };

    /// Resolve the timeline into an ordered list of points suitable for
    /// generating keyframes.
    [[nodiscard]] std::vector<Point> points() const {
        std::vector<Point> result;
        result.push_back(Point{Frame{0}, m_initial_value, EasingCurve{Easing::Linear}});
        for (size_t i = 0; i < m_segments.size(); ++i) {
            // The outgoing easing for point i is the easing of segment i
            // (how we leave point i toward point i+1).
            result.back().outgoing_easing = m_segments[i].easing;
            result.push_back(Point{
                m_segments[i].end_frame,
                m_segments[i].value,
                EasingCurve{Easing::Linear}  // last point has no outgoing
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

// ── Factory function ────────────────────────────────────────────────────────

template <typename T>
[[nodiscard]] inline Timeline<T> timeline(T initial) {
    return Timeline<T>(std::move(initial));
}

} // namespace chronon3d::motion

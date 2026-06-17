#pragma once

#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/types/types.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

namespace chronon3d {

// ── Keyframe Interpolation Mode (like After Effects) ─────────────────────────

enum class KeyInterp {
    Linear,      // straight line between keyframes
    Bezier,      // custom bezier handles (in/out tangent independently adjustable)
    AutoBezier,  // auto-computed tangent based on adjacent keyframes (smooth)
    Hold         // step function — value jumps at keyframe
};

// ── Tangent — controls a bezier handle direction and magnitude ───────────────
// In After Effects, each keyframe has an incoming (left) and outgoing (right)
// handle. The handle is specified as an offset from the keyframe position.

struct Tangent {
    f32 dx{0.0f};  // time offset in frames from the keyframe (positive = forward)
    f32 dy{0.0f};  // value offset from the keyframe value

    constexpr Tangent() = default;
    constexpr Tangent(f32 time_offset, f32 value_offset)
        : dx(time_offset), dy(value_offset) {}

    [[nodiscard]] constexpr bool is_zero() const {
        return (dx < 0.0f ? -dx : dx) < 1e-7f && (dy < 0.0f ? -dy : dy) < 1e-7f;
    }
};

// ── AnimationKeyframe — extends basic keyframe with tangent handles ──────────

struct AnimationKeyframe {
    Frame     frame{0};
    f32       value{0.0f};
    KeyInterp interp{KeyInterp::Linear};
    Tangent   in_tangent;     // left handle (incoming)
    Tangent   out_tangent;    // right handle (outgoing)
    bool      roving{false};  // roving keyframe: auto-timed for constant velocity

    constexpr AnimationKeyframe() = default;
    constexpr AnimationKeyframe(Frame f, f32 v, KeyInterp i = KeyInterp::Linear)
        : frame(f), value(v), interp(i) {}

    // For sorting / binary search
    bool operator<(const AnimationKeyframe& other) const { return frame < other.frame; }
    bool operator<(Frame f) const { return frame < f; }
};

// ── AnimationCurve — manages AnimationKeyframes with AE-like interpolation ──
//
// Usage:
//   AnimationCurve curve;
//   curve.key(Frame{0},   -15.0f, KeyInterp::Bezier, Tangent{-5, 2}, Tangent{5, -2});
//   curve.key(Frame{45},   15.0f, KeyInterp::AutoBezier);
//   curve.key(Frame{90},  -15.0f).roving();  // ← auto-timed for constant velocity
//   curve.compute_roving();
//   f32 val = curve.evaluate(Frame{30});

class AnimationCurve {
public:
    AnimationCurve() = default;
    explicit AnimationCurve(f32 default_value) : m_default_value(default_value) {}

    // ── Fluent builder ───────────────────────────────────────────────────────

    AnimationCurve& key(Frame frame, f32 value, KeyInterp interp = KeyInterp::Linear) {
        m_keyframes.push_back(AnimationKeyframe(frame, value, interp));
        m_sorted = false;
        if (interp == KeyInterp::AutoBezier) m_auto_bezier_dirty = true;
        return *this;
    }

    AnimationCurve& key(Frame frame, f32 value, KeyInterp interp,
                         Tangent in_tan, Tangent out_tan) {
        AnimationKeyframe kf(frame, value, interp);
        kf.in_tangent = in_tan;
        kf.out_tangent = out_tan;
        m_keyframes.push_back(kf);
        m_sorted = false;
        if (interp == KeyInterp::AutoBezier) m_auto_bezier_dirty = true;
        return *this;
    }

    // Mark the last-added keyframe as roving (constant velocity).
    AnimationCurve& roving() {
        if (!m_keyframes.empty()) {
            m_keyframes.back().roving = true;
            m_roving_dirty = true;
        }
        return *this;
    }

    // Set the default value when there are no keyframes or before the first.
    AnimationCurve& set(f32 value) {
        m_default_value = value;
        return *this;
    }

    void compute_roving() const;
    void compute_auto_beziers() const;

    // ── Evaluation ───────────────────────────────────────────────────────────

    /// Sub-frame evaluation (SampleTime) — the primary entry point.
    [[nodiscard]] f32 evaluate(SampleTime time) const {
        return evaluate_double(time.frame);
    }

    /// Double-precision evaluation — core interpolation engine.
    [[nodiscard]] f32 evaluate(double frame) const {
        return evaluate_double(frame);
    }

    /// Integer-frame evaluation (backward compatible).
    [[nodiscard]] f32 evaluate(Frame frame) const {
        return evaluate_double(static_cast<double>(frame));
    }

    /// Check if this curve is animated (has keyframes beyond default).
    [[nodiscard]] bool should_cache(SampleTime time) const {
        if (m_keyframes.empty()) return false;
        const double start_f = static_cast<double>(m_keyframes.front().frame);
        const double end_f   = static_cast<double>(m_keyframes.back().frame);
        return time.frame > start_f && time.frame < end_f;
    }

    // ── Queries ──────────────────────────────────────────────────────────────

    [[nodiscard]] bool empty() const { return m_keyframes.empty(); }
    [[nodiscard]] std::size_t size() const { return m_keyframes.size(); }
    [[nodiscard]] bool is_animated() const { return !m_keyframes.empty(); }

    [[nodiscard]] const std::vector<AnimationKeyframe>& keyframes() const {
        return m_keyframes;
    }

    [[nodiscard]] std::vector<AnimationKeyframe>& keyframes() {
        return m_keyframes;
    }

    void clear() {
        m_keyframes.clear();
        m_roving_dirty = true;
        m_auto_bezier_dirty = true;
    }

    /// Shift every keyframe forward (or backward) by offset frames.
    void shift(Frame offset) {
        for (auto& kf : m_keyframes) {
            kf.frame = std::max(Frame{0}, kf.frame + offset);
        }
        m_sorted = false;
    }

    [[nodiscard]] f32 default_value() const { return m_default_value; }

private:
    void ensure_sorted() const;

    [[nodiscard]] static f32 eval_bezier_segment(
        const AnimationKeyframe& prev, const AnimationKeyframe& next, f32 t);

    [[nodiscard]] static f32 eval_linear_segment(f32 v0, f32 v1, f32 t);

    [[nodiscard]] f32 evaluate_double(double frame) const;

    f32 m_default_value{0.0f};
    mutable std::vector<AnimationKeyframe> m_keyframes;
    mutable bool m_sorted{true};
    mutable bool m_roving_dirty{false};
    mutable bool m_auto_bezier_dirty{false};
};

} // namespace chronon3d

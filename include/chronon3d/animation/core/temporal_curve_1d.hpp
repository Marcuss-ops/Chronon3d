#pragma once

// ============================================================================
// temporal_curve_1d.hpp — 1D temporal curve infrastructure
//
// FASE 19 split: temporal_spatial_curve.hpp was monolithic (~548L) holding
// both time-axis curves and space-axis primitives.  This header owns ONLY the
// 1D time-axis machinery: the path-timing invocation mode = driver of the
// sampler + the TemporalCurve1D class itself.
//
// Spatial primitives (CubicBezier3D, SpatialKeyframe3D, MotionSegment3D,
// MotionPath3D, MotionPathSampler3D) live in `spatial_motion_path.hpp`.
//
// Both headers are re-exported by `temporal_spatial_curve.hpp` (the umbrella
// forwarding header) for backward compatibility with existing call sites
// (5 test files: test_temporal_spatial_curve.cpp + cinematic_motion suite).
// ============================================================================

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/animation/easing/easing.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace chronon3d {

// (PathTimingMode moved to spatial_motion_path.hpp in FASE 19 review pass:
//  it is the spatial sampler's input knob, not a 1D timing primitive.)

// ── TemporalCurve1D — answers "what fraction of the segment have I completed?" ──
//
// Wraps either a simple `EasingCurve` (for presets) or a full `AnimationCurve`
// (for keyframed timing with handles).  Takes a local normalised time ∈ [0,1]
// and maps it through the timing curve, returning the effective progress
// ∈ [0, 1] (clamped).
class TemporalCurve1D {
public:
    TemporalCurve1D() = default;

    /// Construct from a simple easing preset (most common).
    explicit TemporalCurve1D(EasingCurve easing)
        : m_easing(std::move(easing)) {}

    /// Construct from a keyframed timing curve.
    explicit TemporalCurve1D(AnimatedValue<f32> curve)
        : m_curve(std::make_unique<AnimatedValue<f32>>(std::move(curve))) {}

    /// Evaluate normalised progress at local time t ∈ [0, 1].
    [[nodiscard]] f32 evaluate_normalized(f32 t) const {
        t = std::clamp(t, 0.0f, 1.0f);
        if (m_curve && m_curve->is_animated()) {
            // Map [0,1] to the curve's frame range.
            const f32 start_f = static_cast<f32>(m_curve->first_keyframe_time());
            const f32 end_f   = static_cast<f32>(m_curve->last_keyframe_time());
            const f32 range   = end_f - start_f;
            if (range <= 0.0f) return m_easing.apply(t);
            const f32 frame = start_f + t * range;
            const f32 val = m_curve->evaluate(static_cast<double>(frame));
            // Clamp output in case curve goes outside [0,1]
            return std::clamp(val, 0.0f, 1.0f);
        }
        return m_easing.apply(t);
    }

    /// Returns true if the curve does more than simple linear pass-through.
    [[nodiscard]] bool is_non_trivial() const {
        if (m_curve && m_curve->is_animated()) return true;
        return m_easing.preset != Easing::Linear || m_easing.cubic.has_value();
    }

    [[nodiscard]] bool has_animation_curve() const {
        return m_curve != nullptr && m_curve->is_animated();
    }

    // Copy / move support (AnimatedValue<f32> is deep-copied via unique_ptr).
    TemporalCurve1D(const TemporalCurve1D& other)
        : m_easing(other.m_easing)
        , m_curve(other.m_curve ? std::make_unique<AnimatedValue<f32>>(*other.m_curve) : nullptr) {}
    TemporalCurve1D& operator=(const TemporalCurve1D& other) {
        if (this != &other) {
            m_easing = other.m_easing;
            m_curve = other.m_curve ? std::make_unique<AnimatedValue<f32>>(*other.m_curve) : nullptr;
        }
        return *this;
    }
    TemporalCurve1D(TemporalCurve1D&&) noexcept = default;
    TemporalCurve1D& operator=(TemporalCurve1D&&) noexcept = default;

private:
    EasingCurve m_easing{Easing::Linear};
    std::unique_ptr<AnimatedValue<f32>> m_curve;
};

} // namespace chronon3d

#pragma once

#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/easing/interpolate.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/types/types.hpp>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <utility>
#include <vector>

namespace chronon3d {

// ── Interpolation mode for keyframes ──────────────────────────────────────
// Replaces the old AnimationCurve's KeyInterp enum.
// Easing: use the easing curve (default, backward compatible)
// Hold:   step function — value jumps at keyframe
// Bezier: custom temporal tangent handles (in/out independently adjustable)
// AutoBezier: auto-computed temporal tangents from adjacent keyframes
enum class InterpMode {
    Easing,      // default — use EasingCurve
    Hold,        // step to next value at keyframe
    Bezier,      // custom temporal handles
    AutoBezier   // auto-computed temporal tangents
};

// Generic keyframe used by AnimatedValue<T>.
// When T is a spatial type (Vec2/Vec3/Vec4), in_handle and out_handle
// are used as cubic bezier control offsets from value, enabling smooth
// curved paths through multiple keyframes.
template <typename T>
struct Keyframe {
    Frame        frame{0};
    T            value{};
    EasingCurve  easing{Easing::Linear};
    bool         roving{false};  // roving keyframe: auto-timed for constant velocity

    // Interpolation mode (replaces old KeyInterp from AnimationCurve).
    InterpMode   interp{InterpMode::Easing};

    // Temporal tangent handles (After Effects-style).
    // dx = time offset in frames from keyframe (positive = forward)
    // dy = value offset from the keyframe value
    // Used when interp == Bezier or AutoBezier.
    f32 temporal_in_dx{0.0f};
    f32 temporal_in_dy{0.0f};
    f32 temporal_out_dx{0.0f};
    f32 temporal_out_dy{0.0f};

    // Spatial bezier handles: offsets from `value` that control the shape
    // of the cubic bezier path through this keyframe.
    // out_handle → outgoing tangent (pulls the curve toward this point)
    // in_handle  → incoming tangent (pulls the curve from the previous keyframe)
    // Zero by default (straight-line interpolation).
    T out_handle{};
    T in_handle{};

    constexpr Keyframe(Frame f, T v, EasingCurve e = EasingCurve{Easing::Linear}, bool r = false)
        : frame(f), value(v), easing(e), roving(r) {}

    // For std::sort / std::lower_bound
    bool operator<(const Keyframe& other) const { return frame < other.frame; }
    bool operator<(Frame f) const { return frame < f; }
};

// Convenience alias for scalar (f32) keyframes.
using KF = Keyframe<f32>;

// Forward declarations for interpolation helpers.
template <typename T>
inline T interpolate_values(const T& a, const T& b, f32 t, EasingCurve e) {
    if (!e.cubic.has_value() && e.preset == Easing::Hold) return a;
    const f32 eased_t = e.apply(t);
    return a + (b - a) * eased_t;
}

// NOTE: KeyframeTrack<T> is now a type alias for AnimatedValue<T>,
// defined in animated_value.hpp.  Include animated_value.hpp for
// KeyframeTrack, keyframes<T>(), and the full animation engine.

} // namespace chronon3d

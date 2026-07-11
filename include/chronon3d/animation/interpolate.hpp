#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// interpolate.hpp — Top-level animation convenience header (F3.A)
//
// Single include for all common animation helpers.  Re-exports the
// existing sub-module functions with simplified signatures and adds
// new free-function helpers for composition-level animation patterns.
//
// Usage:
//   #include <chronon3d/animation/interpolate.hpp>
//
//   // Fade in over 15 frames
//   f32 opacity = interpolate(ctx.frame, {0, 15}, {0.0f, 1.0f});
//
//   // With easing
//   f32 scale = interpolate(ctx.frame, {0, 30}, {0.5f, 1.0f}, Easing::OutCubic);
//
//   // Spring animation
//   f32 pos = spring(ctx, -300.0f, 0.0f, Spring::Gentle);
//
//   // Sequence of segments
//   f32 val = sequence(ctx.frame, {
//       {0, 15, 0.0f, 1.0f, Easing::OutCubic},
//       {15, 30, 1.0f, 0.5f, Easing::InOutSine},
//   });
//
//   // Loop a frame range
//   Frame local = loop(ctx.frame, Frame{60});  // 0..59 repeating
//
//   // Delay animation start
//   f32 fade = delay(ctx.frame, Frame{30}, 0.0f, 1.0f, Frame{30}, Frame{60});
//
//   // Apply easing to normalized t
//   f32 eased = ease(0.5f, Easing::OutBounce);
//
//   // Clamp
//   f32 safe = clamp(value, 0.0f, 1.0f);
//
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/easing/spring.hpp>
#include <chronon3d/animation/effects/stagger.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/time.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace chronon3d {

// ── Range types for brace-init syntax ──────────────────────────────────────

/// Frame range for interpolate: {start_frame, end_frame}.
/// Enables: interpolate(frame, {0, 15}, {0.0f, 1.0f})
struct FrameRange {
    Frame start{0};
    Frame end{0};
};

/// Value range for interpolate: {from_value, to_value}.
struct ValueRange {
    f32 from{0.0f};
    f32 to{1.0f};
};

/// Animation segment for sequence().
struct Segment {
    Frame start{0};
    Frame end{0};
    f32 from{0.0f};
    f32 to{1.0f};
    EasingCurve easing{Easing::Linear};
};

// ── interpolate — frame-based value interpolation ──────────────────────────

/// Interpolate a value over a frame range with optional easing.
///
/// Maps `frame` from [range.start, range.end] to [values.from, values.to].
/// Frame is clamped to the range by default (no overshoot).
///
/// Example:
///   f32 opacity = interpolate(ctx.frame, {0, 15}, {0.0f, 1.0f});
///   f32 scale   = interpolate(ctx.frame, {0, 30}, {0.5f, 1.0f}, Easing::OutCubic);
[[nodiscard]] inline f32 interpolate(
    Frame frame,
    FrameRange range,
    ValueRange values,
    EasingCurve easing = EasingCurve{Easing::Linear}
) {
    const f32 f = static_cast<f32>(frame.integral());
    const f32 s = static_cast<f32>(range.start.integral());
    const f32 e = static_cast<f32>(range.end.integral());
    if (e == s) return values.from;
    f32 t = (f - s) / (e - s);
    t = std::clamp(t, 0.0f, 1.0f);
    t = easing.apply(t);
    return values.from + (values.to - values.from) * t;
}

/// Interpolate with explicit scalar arguments (backward-compatible overload).
[[nodiscard]] inline f32 interpolate(
    Frame frame,
    Frame start,
    Frame end,
    f32 from,
    f32 to,
    EasingCurve easing = EasingCurve{Easing::Linear}
) {
    return interpolate(frame, FrameRange{start, end}, ValueRange{from, to}, easing);
}

/// Interpolate between two Vec2 positions over a frame range.
[[nodiscard]] inline Vec2 interpolate(
    Frame frame,
    FrameRange range,
    Vec2 from,
    Vec2 to,
    EasingCurve easing = EasingCurve{Easing::Linear}
) {
    const f32 t = interpolate(frame, range, ValueRange{0.0f, 1.0f}, easing);
    return from + (to - from) * t;
}

/// Interpolate between two Vec3 positions over a frame range.
[[nodiscard]] inline Vec3 interpolate(
    Frame frame,
    FrameRange range,
    Vec3 from,
    Vec3 to,
    EasingCurve easing = EasingCurve{Easing::Linear}
) {
    const f32 t = interpolate(frame, range, ValueRange{0.0f, 1.0f}, easing);
    return from + (to - from) * t;
}

// ── spring — physics-based spring animation ────────────────────────────────

/// Spring animation from `from` to `to`, driven by frame number.
/// Time is computed as frame / fps.
///
/// Example:
///   f32 pos = spring(ctx.frame, -300.0f, 0.0f, Spring::Gentle);
[[nodiscard]] inline f32 spring(
    Frame frame,
    FrameRate fps,
    f32 from,
    f32 to,
    const SpringConfig& config = {}
) {
    return ::chronon3d::spring(frame, fps, from, to, config);
}

// ── sequence — evaluate a sequence of animation segments ───────────────────

/// Evaluate a sequence of animation segments in order.
///
/// Each segment defines a [start, end) frame range with its own from/to
/// values and easing curve.  Before the first segment, `before` is returned.
/// After the last segment, the last segment's `to` value is held.
///
/// Example:
///   f32 val = sequence(ctx.frame, {
///       {0, 15, 0.0f, 1.0f, Easing::OutCubic},
///       {15, 30, 1.0f, 0.5f, Easing::InOutSine},
///   });
[[nodiscard]] inline f32 sequence(
    Frame frame,
    std::initializer_list<Segment> segments,
    f32 before = 0.0f
) {
    if (segments.size() == 0) return before;

    const i64 f = frame.integral();
    const auto* segs = segments.begin();
    const auto count = segments.size();

    // Before first segment
    if (f < segs[0].start.integral()) return before;

    // Find active segment
    for (size_t i = 0; i < count; ++i) {
        const auto& seg = segs[i];
        const i64 s = seg.start.integral();
        const i64 e = seg.end.integral();
        if (f >= s && f < e) {
            return interpolate(frame, FrameRange{seg.start, seg.end},
                               ValueRange{seg.from, seg.to}, seg.easing);
        }
    }

    // After last segment — hold last value
    return segs[count - 1].to;
}

// ── loop — wrap a frame into a repeating period ────────────────────────────

/// Wrap a frame into a repeating period.
///
/// Returns the local frame within [0, period).  Useful for looping
/// animations that repeat every N frames.
///
/// Example:
///   Frame local = loop(ctx.frame, Frame{60});  // 0..59 repeating
///   f32 pulse = interpolate(local, {0, 30}, {0.0f, 1.0f});
[[nodiscard]] inline Frame loop(Frame frame, Frame period) {
    if (period.integral() <= 0) return Frame{0};
    const i64 f = frame.integral();
    const i64 p = period.integral();
    if (f < 0) return Frame{0};
    return Frame{f % p};
}

/// Loop with an offset (start frame of the loop).
[[nodiscard]] inline Frame loop(Frame frame, Frame period, Frame offset) {
    if (period.integral() <= 0) return Frame{0};
    const i64 f = frame.integral() - offset.integral();
    if (f < 0) return Frame{0};
    return Frame{f % period.integral()};
}

/// Stagger delay for an element at a given rank.
/// Re-exports compute_stagger_delay() from stagger.hpp for convenience.
///
/// Example:
///   Frame d = stagger_delay(i, total, StaggerConfig{Frame{4}, Easing::OutCubic});
[[nodiscard]] inline Frame stagger_delay(
    size_t rank,
    size_t total,
    const StaggerConfig& config = {}
) {
    return compute_stagger_delay(rank, total, config);
}

/// Loop with ping-pong: the value reverses direction on every other cycle.
///
/// Example:
///   Frame local = loop_pingpong(ctx.frame, Frame{60});  // 0..59..0..59..
[[nodiscard]] inline Frame loop_pingpong(Frame frame, Frame period) {
    if (period.integral() <= 0) return Frame{0};
    const i64 f = frame.integral();
    const i64 p = period.integral();
    const i64 cycle = f / p;
    const i64 remainder = f % p;
    return (cycle % 2 == 0) ? Frame{remainder} : Frame{p - remainder};
}

// ── delay — delay the start of an animation ────────────────────────────────

/// Delay an animation by `delay_frames`.
///
/// Returns `value_before` until `delay_frames` have elapsed, then
/// interpolates from `from` to `to` over [start, end).
///
/// Example:
///   f32 fade = delay(ctx.frame, Frame{30}, 0.0f, 1.0f, Frame{30}, Frame{60});
///   // frame 0-29: returns 0.0f (waiting)
///   // frame 30-59: interpolates 0.0f → 1.0f
[[nodiscard]] inline f32 delay(
    Frame frame,
    Frame delay_frames,
    f32 value_before,
    f32 from,
    f32 to,
    Frame start,
    Frame end,
    EasingCurve easing = EasingCurve{Easing::Linear}
) {
    if (frame.integral() < delay_frames.integral()) return value_before;
    return interpolate(frame, FrameRange{start, end}, ValueRange{from, to}, easing);
}

/// Simpler delay: waits `delay_frames`, then fades from `from` to `to`
/// over `duration` frames.
///
/// Example:
///   f32 opacity = delay(ctx.frame, Frame{15}, 0.0f, 1.0f, Frame{15});
///   // waits 15 frames, then interpolates 0→1 over next 15 frames
[[nodiscard]] inline f32 delay(
    Frame frame,
    Frame delay_frames,
    f32 from,
    f32 to,
    Frame duration,
    EasingCurve easing = EasingCurve{Easing::Linear}
) {
    return delay(frame, delay_frames, from, from, to,
                 delay_frames, Frame{delay_frames.integral() + duration.integral()}, easing);
}

// ── ease — apply easing to a normalized t value ────────────────────────────

/// Apply an easing preset to a normalized [0, 1] value.
///
/// Example:
///   f32 t = ease(0.5f, Easing::OutBounce);  // ~0.76
[[nodiscard]] inline f32 ease(f32 t, Easing type) {
    return easing::apply(type, std::clamp(t, 0.0f, 1.0f));
}

/// Apply an easing curve (preset or cubic bezier) to a normalized [0, 1] value.
[[nodiscard]] inline f32 ease(f32 t, EasingCurve curve) {
    return curve.apply(std::clamp(t, 0.0f, 1.0f));
}

// ── clamp — clamp a value to a range ───────────────────────────────────────

/// Clamp a value to [min_val, max_val].
/// Thin wrapper over std::clamp for consistency with the animation API.
[[nodiscard]] inline f32 clamp(f32 value, f32 min_val, f32 max_val) {
    return std::clamp(value, min_val, max_val);
}

/// Time-based clamp: returns `value` only when frame is within [start, end).
/// Returns `outside` otherwise.
[[nodiscard]] inline f32 clamp(
    f32 value,
    Frame frame,
    Frame start,
    Frame end,
    f32 outside = 0.0f
) {
    if (frame.integral() < start.integral() || frame.integral() >= end.integral()) {
        return outside;
    }
    return value;
}

// ── map — remap a value from one range to another ──────────────────────────

/// Remap `value` from [in_min, in_max] to [out_min, out_max].
/// Clamped by default.
[[nodiscard]] inline f32 map(
    f32 value,
    f32 in_min, f32 in_max,
    f32 out_min, f32 out_max,
    bool clamped = true
) {
    if (in_max == in_min) return out_min;
    f32 t = (value - in_min) / (in_max - in_min);
    if (clamped) t = std::clamp(t, 0.0f, 1.0f);
    return out_min + (out_max - out_min) * t;
}

/// Remap a frame from one range to [0, 1] (normalized progress).
[[nodiscard]] inline f32 progress(Frame frame, Frame start, Frame end) {
    return map(static_cast<f32>(frame.integral()),
               static_cast<f32>(start.integral()),
               static_cast<f32>(end.integral()),
               0.0f, 1.0f);
}

// ── M1.8 §3A extension: 6 additional helpers (17 total) ──────────────────
//
// The following 6 helpers are ADDITIVE (forward-only, no breakage to the
// pre-§3A surface).  They reach the 17-helper count specified by the M1.8
// §3A "Animation Helpers" milestone: 4 existing categories (interpolate /
// spring / sequence / loop / delay / ease / clamp / map / progress /
// stagger_delay) + 1 alias + 5 new value-level utilities.
//
// Anti-duplicazione honour: every helper reuses an existing primitive
// (`stagger` = alias of `stagger_delay`; `tween` = alias of `interpolate`;
// `linear` = alias of `ease(t, Easing::Linear)`; `clamp_progress` = alias
// of `clamp(t, 0, 1)`).  `stagger_value` and `frame_to_seconds` are the
// only NEW pure-math helpers; both are stateless and one-liners.

// ── stagger — alias of stagger_delay (more user-friendly name) ───────────

/// Stagger delay for an element at a given rank.  Short alias of
/// `stagger_delay(rank, total, config)` — the more user-friendly name.
///
/// Example:
///   Frame d = stagger(i, total, StaggerConfig{Frame{4}, Easing::OutCubic});
[[nodiscard]] inline Frame stagger(
    size_t rank,
    size_t total,
    const StaggerConfig& config = {}
) {
    return stagger_delay(rank, total, config);
}

// ── stagger_value — value-level stagger helper ────────────────────────────

/// Apply a stagger delay to a base value: returns `value` shifted by the
/// stagger offset (positive for delayed).  Convenience for chaining
/// stagger logic into a numeric expression without manually adding
/// `stagger_delay(rank, total, config).integral()` to a frame counter.
///
/// Example:
///   f32 start_time = stagger_value(rank, total, 0.0f, config);  // delay
///   f32 eased      = interpolate(frame, start_time, start_time+30.0f, 0, 1);
[[nodiscard]] inline f32 stagger_value(
    f32 base_value,
    size_t rank,
    size_t total,
    const StaggerConfig& config = {}
) {
    return base_value + static_cast<f32>(stagger(rank, total, config).integral());
}

// ── tween — alias of interpolate (animation-industry name) ────────────────

/// Tween between two values over a frame range.  Short alias of
/// `interpolate(frame, range, values, easing)` — matches the animation-
/// industry term "tween" (in-betweening).
///
/// Example:
///   f32 opacity = tween(ctx.frame, {0, 15}, {0.0f, 1.0f}, Easing::OutCubic);
[[nodiscard]] inline f32 tween(
    Frame frame,
    FrameRange range,
    ValueRange values,
    EasingCurve easing = EasingCurve{Easing::Linear}
) {
    return interpolate(frame, range, values, easing);
}

// ── frame_to_seconds — convert a frame + fps to seconds ───────────────────

/// Convert a frame number + frame rate to a wall-clock seconds value.
///
/// Example:
///   f32 t = frame_to_seconds(Frame{60}, FrameRate{30, 1});  // 2.0f
[[nodiscard]] inline f32 frame_to_seconds(Frame frame, FrameRate fps) {
    return static_cast<f32>(fps.to_seconds(frame));
}

// ── linear — alias of ease(t, Easing::Linear) (most common case) ─────────

/// Apply linear easing (identity) to a normalized [0, 1] value.
/// Short alias of `ease(t, Easing::Linear)` — the most common easing in
/// animation code; avoids the ceremony of spelling out the enum.
///
/// Example:
///   f32 t = linear(0.5f);  // 0.5f
[[nodiscard]] inline f32 linear(f32 t) {
    return ease(t, Easing::Linear);
}

// ── clamp_progress — alias of clamp(t, 0, 1) (the most common clamp) ─────

/// Clamp a normalized [0, 1] progress value.  Short alias of
/// `clamp(t, 0.0f, 1.0f)` — avoids the magic numbers at the call site.
///
/// Example:
///   f32 p = clamp_progress(1.2f);  // 1.0f
[[nodiscard]] inline f32 clamp_progress(f32 t) {
    return std::clamp(t, 0.0f, 1.0f);
}

} // namespace chronon3d

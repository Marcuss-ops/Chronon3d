#pragma once

// ── Temporal / Spatial Curve Split ─────────────────────────────────────────
//
// After Effects separates temporal interpolation (how fast we move between
// keyframes) from spatial interpolation (the shape of the path in space).
//
//   TemporalCurve1D    → "At what percentage am I done?"
//   CubicBezier3D      → "Where am I in space at that percentage?"
//   MotionSegment3D    → one keyframe pair (timing + 3D path)
//   MotionPath3D       → sequence of segments
//   MotionPathSampler3D → evaluates the whole path at a SampleTime
//
// This module is pure math: no camera, no render graph, no scene.
// Integration with CameraRig / AnimatedCamera comes in a follow-up PR.
// ────────────────────────────────────────────────────────────────────────────

#include <chronon3d/animation/core/animation_curve.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/path/spatial_bezier_path.hpp>  // for PathSample
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace chronon3d {



// ── PathTimingMode — how the spatial parameter u is derived from progress ──
//
// * Parametric:      u = progress  (uniform in parameter space)
// * ArcLength:       u = arc-length reparameterisation (uniform speed)
// * EasedArcLength:  timing curve modifies intentional speed, but position
//                    is still parameterised in arc-length for clean motion

enum class PathTimingMode {
    Parametric,
    ArcLength,
    EasedArcLength
};

// ── CubicBezier3D — a single cubic bezier segment in 3D space ──────────────
//
// Defined by four control points:
//   P0 = start position
//   P1 = P0 + out_handle (first handle)
//   P2 = P3 + in_handle  (second handle)
//   P3 = end position
//
// B(u) = (1-u)³P₀ + 3(1-u)²u·P₁ + 3(1-u)u²·P₂ + u³·P₃

struct CubicBezier3D {
    Vec3 p0{0.0f};
    Vec3 p1{0.0f};
    Vec3 p2{0.0f};
    Vec3 p3{0.0f};

    constexpr CubicBezier3D() = default;
    constexpr CubicBezier3D(Vec3 a, Vec3 b, Vec3 c, Vec3 d)
        : p0(a), p1(b), p2(c), p3(d) {}

    /// Evaluate position at u ∈ [0, 1].
    [[nodiscard]] Vec3 position(double u) const noexcept {
        const double v = 1.0 - u;
        return static_cast<f32>(v * v * v) * p0
             + static_cast<f32>(3.0 * v * v * u) * p1
             + static_cast<f32>(3.0 * v * u * u) * p2
             + static_cast<f32>(u * u * u) * p3;
    }

    /// Evaluate first derivative / velocity at u ∈ [0, 1].
    [[nodiscard]] Vec3 derivative(double u) const noexcept {
        const double v = 1.0 - u;
        return static_cast<f32>(3.0 * v * v) * (p1 - p0)
             + static_cast<f32>(6.0 * v * u) * (p2 - p1)
             + static_cast<f32>(3.0 * u * u) * (p3 - p2);
    }

    /// Evaluate the unit-length tangent direction at u.
    /// Returns forward vector (p0→p1) if the derivative is degenerate.
    [[nodiscard]] Vec3 tangent_at(double u) const noexcept {
        const Vec3 d = derivative(u);
        const f32 len2 = glm::length2(d);
        if (len2 > 1e-12f) return glm::normalize(d);
        // Fallback: direction from p0 to p1 (or p0 to p3 if degenerate).
        const Vec3 fallback = glm::normalize(p1 - p0);
        const f32 fallback_len2 = glm::length2(fallback);
        return (fallback_len2 > 1e-12f) ? fallback : Vec3{0.0f, 0.0f, -1.0f};
    }

    /// Evaluate position and normalised forward direction at u.
    [[nodiscard]] PathSample sample(double u) const noexcept {
        const Vec3 p = position(u);
        const Vec3 fwd = tangent_at(u);
        return {p, fwd};
    }

    /// Build from start/end positions and spatial handles (offsets from position).
    static CubicBezier3D from_handles(
        Vec3 start_pos, Vec3 out_handle,
        Vec3 end_pos,   Vec3 in_handle
    ) noexcept {
        return {
            start_pos,
            start_pos + out_handle,
            end_pos + in_handle,
            end_pos
        };
    }

    /// Build from start/end positions and unit-length tangent directions with
    /// scalar magnitudes.  The handles are set to `tangent * magnitude`
    /// relative to the keyframe positions, producing a smooth curve that
    /// passes through both endpoints with the given exit/entry directions.
    ///
    /// Typical AE-like usage: magnitude ≈ duration / 3  for smooth auto-bezier.
    static CubicBezier3D from_tangents(
        Vec3 start_pos, Vec3 start_tangent, f32 start_mag,
        Vec3 end_pos,   Vec3 end_tangent,   f32 end_mag
    ) noexcept {
        const Vec3 out_h = start_tangent * start_mag;
        const Vec3 in_h  = end_tangent * end_mag;
        return {
            start_pos,
            start_pos + out_h,
            end_pos + in_h,
            end_pos
        };
    }

    // ── Continuity checks ────────────────────────────────────────────────

    /// C0 (positional) continuity: the end of `this` segment matches the
    /// start of `next` within `tolerance`.
    [[nodiscard]] bool is_c0_continuous_with(
        const CubicBezier3D& next,
        f32 tolerance = 1e-4f
    ) const noexcept {
        return glm::length(position(1.0) - next.position(0.0)) <= tolerance;
    }

    /// C1 (tangential) continuity: the end derivative of `this` matches the
    /// start derivative of `next` within `tolerance`.
    ///
    /// Both magnitude AND direction must match (parametrisation-velocity
    /// continuity).  For visual-only smoothness use `is_geometric_c1_with()`.
    [[nodiscard]] bool is_c1_continuous_with(
        const CubicBezier3D& next,
        f32 tolerance = 1e-4f
    ) const noexcept {
        return glm::length(derivative(1.0) - next.derivative(0.0)) <= tolerance;
    }

    /// Geometric C1 (G1) continuity: the end tangent of `this` is parallel
    /// to the start tangent of `next` (direction only, magnitude ignored).
    [[nodiscard]] bool is_geometric_c1_with(
        const CubicBezier3D& next,
        f32 dot_tolerance = 0.9999f
    ) const noexcept {
        const Vec3 t1 = tangent_at(1.0);
        const Vec3 t2 = next.tangent_at(0.0);
        return glm::abs(glm::dot(t1, t2)) >= dot_tolerance;
    }

    bool operator==(const CubicBezier3D&) const = default;
};

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
    explicit TemporalCurve1D(AnimationCurve curve)
        : m_curve(std::make_unique<AnimationCurve>(std::move(curve))) {}

    /// Evaluate normalised progress at local time t ∈ [0, 1].
    [[nodiscard]] f32 evaluate_normalized(f32 t) const {
        t = std::clamp(t, 0.0f, 1.0f);
        if (m_curve && !m_curve->empty()) {
            // Map [0,1] to the curve's frame range.
            const f32 start_f = static_cast<f32>(m_curve->keyframes().front().frame);
            const f32 end_f   = static_cast<f32>(m_curve->keyframes().back().frame);
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
        if (m_curve && !m_curve->empty()) return true;
        return m_easing.preset != Easing::Linear || m_easing.cubic.has_value();
    }

    [[nodiscard]] bool has_animation_curve() const {
        return m_curve != nullptr && !m_curve->empty();
    }

    // Copy / move support (AnimationCurve is deep-copied via unique_ptr).
    TemporalCurve1D(const TemporalCurve1D& other)
        : m_easing(other.m_easing)
        , m_curve(other.m_curve ? std::make_unique<AnimationCurve>(*other.m_curve) : nullptr) {}
    TemporalCurve1D& operator=(const TemporalCurve1D& other) {
        if (this != &other) {
            m_easing = other.m_easing;
            m_curve = other.m_curve ? std::make_unique<AnimationCurve>(*other.m_curve) : nullptr;
        }
        return *this;
    }
    TemporalCurve1D(TemporalCurve1D&&) = default;
    TemporalCurve1D& operator=(TemporalCurve1D&&) = default;

private:
    EasingCurve m_easing{Easing::Linear};
    std::unique_ptr<AnimationCurve> m_curve;
};

// ── SpatialKeyframe3D — a spatial waypoint at a specific time ──────────────
//
// Each keyframe stores a 3D position and two spatial handles (offsets from
// position).  The handles control the shape of the Bezier curve in space,
// independent of the temporal easing between keyframes.

struct SpatialKeyframe3D {
    SampleTime time{};                 // when this keyframe occurs
    Vec3 value{0.0f, 0.0f, 0.0f};    // spatial position

    // Spatial handles: offsets from `value`.
    // in_handle  → incoming tangent (pulls the curve toward this point)
    // out_handle → outgoing tangent
    Vec3 in_handle{0.0f, 0.0f, 0.0f};
    Vec3 out_handle{0.0f, 0.0f, 0.0f};

    constexpr SpatialKeyframe3D() = default;
    constexpr SpatialKeyframe3D(SampleTime t, Vec3 v)
        : time(t), value(v) {}
};

// ── make_auto_smooth (free function — depends on SpatialKeyframe3D) ──────
//
/// Auto-smooth factory: computes handles that produce smooth (C1)
/// interpolation through the middle keyframe when chained.
///
/// Uses the standard Catmull-Rom → Bezier conversion:
///   out_handle = (next - prev) / 6
///   in_handle  = -(next - prev) / 6
/// with kTension = 1/3 (standard auto-bezier default).
///
/// @param prev   keyframe before this segment (for incoming direction)
/// @param curr   start of this segment
/// @param next   end of this segment
/// @param after  keyframe after this segment (for outgoing direction)
/// @param tension  scale factor for handle length (default 1/3 = AE auto-bezier)
[[nodiscard]] inline CubicBezier3D make_auto_smooth(
    const SpatialKeyframe3D& prev,
    const SpatialKeyframe3D& curr,
    const SpatialKeyframe3D& next,
    const SpatialKeyframe3D& after,
    f32 tension = 1.0f / 3.0f
) noexcept {
    // Slope at the current keyframe (incoming → outgoing)
    const Vec3 dv = next.value - prev.value;
    const Vec3 out_handle = dv * tension;
    // Slope at the next keyframe
    const Vec3 dv_after = after.value - curr.value;
    const Vec3 in_handle = dv_after * -tension;
    return CubicBezier3D::from_handles(curr.value, out_handle, next.value, in_handle);
}

// ── MotionSegment3D — one segment between two keyframes ────────────────────
//
// Binds a `TemporalCurve1D` (timing) to a `CubicBezier3D` (path in space).
// The segment spans from `start.time` to `end.time`.

struct MotionSegment3D {
    SampleTime start_time{};
    SampleTime end_time{};
    TemporalCurve1D timing{};
    CubicBezier3D path{};
    bool use_arc_length{false};

    /// Duration in frames (may be fractional).
    [[nodiscard]] double duration_frames() const {
        return end_time.frame - start_time.frame;
    }

    /// Build from two spatial keyframes with a timing curve.
    static MotionSegment3D from_keyframes(
        const SpatialKeyframe3D& a,
        const SpatialKeyframe3D& b,
        TemporalCurve1D timing = TemporalCurve1D{},
        bool arc_length = false
    ) {
        return {
            .start_time = a.time,
            .end_time   = b.time,
            .timing     = std::move(timing),
            .path       = CubicBezier3D::from_handles(
                a.value, a.out_handle,
                b.value, b.in_handle),
            .use_arc_length = arc_length
        };
    }
};

// ── MotionPath3D — a sequence of connected spatial segments ────────────────
//
// Built from a list of `SpatialKeyframe3D` with per-segment timing curves.

class MotionPath3D {
public:
    MotionPath3D() = default;

    // ── Builder ──────────────────────────────────────────────────────────

    MotionPath3D& add_keyframe(SpatialKeyframe3D kf) {
        m_keyframes.push_back(std::move(kf));
        m_segments_dirty = true;
        return *this;
    }

    /// All segments share the same timing curve.
    MotionPath3D& build_segments_uniform(TemporalCurve1D timing = {}) {
        build_segments(std::move(timing), false);
        return *this;
    }

    /// Build segments from the accumulated keyframes.
    /// If keyframes < 2, no segments are produced.
    void build_segments(TemporalCurve1D timing = {}, bool arc_length = false) {
        m_segments.clear();
        if (m_keyframes.size() < 2) {
            m_segments_dirty = false;
            return;
        }
        for (size_t i = 0; i + 1 < m_keyframes.size(); ++i) {
            m_segments.push_back(MotionSegment3D::from_keyframes(
                m_keyframes[i], m_keyframes[i + 1], timing, arc_length));
        }
        m_segments_dirty = false;
    }

    /// Number of segments.
    [[nodiscard]] size_t segment_count() const { return m_segments.size(); }
    [[nodiscard]] bool empty() const { return m_segments.empty(); }
    [[nodiscard]] const std::vector<SpatialKeyframe3D>& keyframes() const { return m_keyframes; }
    [[nodiscard]] const std::vector<MotionSegment3D>& segments() const { return m_segments; }

    /// Find the segment containing a given SampleTime.
    /// Returns index into segments() or -1 if not found.
    [[nodiscard]] int find_segment(const SampleTime& time) const {
        for (size_t i = 0; i < m_segments.size(); ++i) {
            if (time.frame >= m_segments[i].start_time.frame &&
                time.frame <= m_segments[i].end_time.frame) {
                return static_cast<int>(i);
            }
        }
        // Before first segment
        if (!m_segments.empty() && time.frame < m_segments[0].start_time.frame)
            return 0;
        // After last segment
        if (!m_segments.empty())
            return static_cast<int>(m_segments.size()) - 1;
        return -1;
    }

    void clear() {
        m_keyframes.clear();
        m_segments.clear();
        m_segments_dirty = true;
    }

private:
    std::vector<SpatialKeyframe3D> m_keyframes;
    std::vector<MotionSegment3D> m_segments;
    bool m_segments_dirty{true};
};

// ── MotionPathSampler3D — evaluates a MotionPath3D at a given time ─────────
//
// The sampler takes a `MotionPath3D` and a `SampleTime`, finds the correct
// segment, computes the local progress through the timing curve, and samples
// the spatial bezier to return a `PathSample`.

class MotionPathSampler3D {
public:
    MotionPathSampler3D() = default;

    /// Sample the path at a given SampleTime.
    /// Returns the position + forward direction.
    [[nodiscard]] PathSample sample(
        const MotionPath3D& path,
        SampleTime time,
        PathTimingMode mode = PathTimingMode::Parametric
    ) const {
        if (path.empty()) {
            return {Vec3{0.0f}, Vec3{0.0f, 0.0f, -1.0f}};
        }

        const auto& segments = path.segments();
        const int idx = path.find_segment(time);
        const auto& seg = segments[static_cast<size_t>(std::max(0, idx))];

        // Compute local normalised time ∈ [0, 1] within the segment.
        const double duration = seg.duration_frames();
        if (duration <= 0.0) {
            return seg.path.sample(0.0);
        }

        double local = (time.frame - seg.start_time.frame) / duration;
        local = std::clamp(local, 0.0, 1.0);

        // Apply the temporal curve to get the effective progress.
        const f32 progress = seg.timing.evaluate_normalized(
            static_cast<f32>(local));

        // Determine the spatial parameter `u` based on the timing mode.
        // TODO(PR5): Implement ArcLength and EasedArcLength modes
        // (requires ArcLengthLUT integration).
        const double u = static_cast<double>(progress);

        return seg.path.sample(u);
    }
};

} // namespace chronon3d

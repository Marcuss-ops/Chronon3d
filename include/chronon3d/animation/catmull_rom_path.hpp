#pragma once

#include <chronon3d/animation/easing.hpp>
#include <chronon3d/animation/spatial_bezier_path.hpp>  // for AutoOrientMode
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/model/camera_2_5d.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace chronon3d {

// ── CatmullRomPath3D — interpolating spline through 3D control points ────────
//
// Unlike the cubic Bezier path (spatial_bezier_path.hpp), Catmull-Rom is an
// *interpolating* spline: the curve passes through every control point. The
// tangent at each control point is derived from the neighboring points, so
// animators only need to specify the waypoints they want the camera to visit
// — no manual in/out handles to tune.
//
// Two parameterizations are supported:
//   - Uniform   (α = 0.5)  standard Catmull-Rom; simple, can produce cusps
//   - Centripetal (α = 0.0) avoids cusps and self-intersections (recommended)
//
// The path wraps phantom endpoints depending on the boundary mode:
//   - Clamped: phantom points are mirrored reflections of the first/last CP
//   - Open:    phantom points duplicate the first/last CP (zero velocity)
//   - Closed:  the path wraps around and P[n] == P[0]
//
// Usage:
//   CatmullRomPath3D path;
//   path.set_boundary(BoundaryMode::Clamped);
//   path.add_waypoint({0, 0, -1200});
//   path.add_waypoint({200, 50, -1000});
//   path.add_waypoint({400, 0, -800});
//   Vec3 pos = path.evaluate(0.5f);   // curve passes through the 3 points
//   Vec3 tan = path.tangent_at(0.5f);

enum class CatmullRomBoundary {
    Clamped, // reflect first/last (smooth, default)
    Open,    // duplicate first/last (zero velocity at endpoints)
    Closed,  // wrap around (P[n] connects back to P[0])
};

enum class CatmullRomAlpha {
    Uniform     = 0, // t_i+1 - t_i == 1
    Centripetal = 1, // t_i+1 - t_i = |P_i+1 - P_i|^0.5  (avoids cusps)
    Chordal     = 2, // t_i+1 - t_i = |P_i+1 - P_i|         (most stable)
};

struct CatmullRomWaypoint {
    Vec3 position{0.0f, 0.0f, 0.0f};

    constexpr CatmullRomWaypoint() = default;
    constexpr explicit CatmullRomWaypoint(Vec3 p) : position(p) {}
};

class CatmullRomPath3D {
public:
    CatmullRomPath3D() = default;

    // ── Configuration ────────────────────────────────────────────────────────

    CatmullRomPath3D& set_boundary(CatmullRomBoundary b) {
        m_boundary = b;
        m_param_dirty = true;
        return *this;
    }

    CatmullRomPath3D& set_alpha(CatmullRomAlpha a) {
        m_alpha = a;
        m_param_dirty = true;
        return *this;
    }

    // ── Builder ──────────────────────────────────────────────────────────────

    CatmullRomPath3D& add_waypoint(Vec3 position) {
        m_waypoints.push_back(CatmullRomWaypoint{position});
        m_param_dirty = true;
        m_arc_length_dirty = true;
        return *this;
    }

    CatmullRomPath3D& add_waypoints(std::initializer_list<Vec3> pts) {
        for (auto p : pts) m_waypoints.push_back(CatmullRomWaypoint{p});
        m_param_dirty = true;
        m_arc_length_dirty = true;
        return *this;
    }

    void clear() {
        m_waypoints.clear();
        m_t_values.clear();
        m_samples.clear();
        m_arc_lengths.clear();
        m_total_arc_length = 0.0f;
        m_param_dirty = true;
        m_arc_length_dirty = true;
    }

    // ── Queries ──────────────────────────────────────────────────────────────

    [[nodiscard]] std::size_t waypoint_count() const { return m_waypoints.size(); }
    [[nodiscard]] std::size_t segment_count() const {
        if (m_waypoints.size() < 2) return 0;
        if (m_boundary == CatmullRomBoundary::Closed) return m_waypoints.size();
        return m_waypoints.size() - 1;
    }
    [[nodiscard]] bool empty() const { return m_waypoints.empty(); }
    [[nodiscard]] CatmullRomBoundary boundary() const { return m_boundary; }
    [[nodiscard]] CatmullRomAlpha alpha_mode() const { return m_alpha; }

    [[nodiscard]] const std::vector<CatmullRomWaypoint>& waypoints() const { return m_waypoints; }

    // ── Evaluation ───────────────────────────────────────────────────────────

    // Evaluate the position at normalized t ∈ [0, 1] across the entire path.
    [[nodiscard]] Vec3 evaluate(f32 t) const {
        if (m_waypoints.empty()) return Vec3{0.0f};
        if (m_waypoints.size() == 1) return m_waypoints[0].position;

        t = std::clamp(t, 0.0f, 1.0f);

        const CatmullRomBoundary boundary = effective_boundary();
        if (boundary == CatmullRomBoundary::Closed) {
            return eval_segment_closed(t);
        }
        return eval_segment_open(t);
    }

    // Evaluate the first derivative (tangent) at normalized t.
    [[nodiscard]] Vec3 tangent_at(f32 t) const {
        if (m_waypoints.size() < 2) return Vec3{0.0f};

        t = std::clamp(t, 0.0f, 1.0f);

        const CatmullRomBoundary boundary = effective_boundary();
        if (boundary == CatmullRomBoundary::Closed) {
            return tangent_segment_closed(t);
        }
        return tangent_segment_open(t);
    }

    // Normalized forward direction at t (zero if degenerate).
    [[nodiscard]] Vec3 forward_at(f32 t) const {
        Vec3 tan = tangent_at(t);
        f32 len = glm::length(tan);
        return (len > 1e-6f) ? (tan / len) : Vec3{0.0f, 0.0f, -1.0f};
    }

    // ── Arc-length parameterization ──────────────────────────────────────────

    [[nodiscard]] Vec3 evaluate_arc_length(f32 t) const {
        if (m_waypoints.size() < 2) return evaluate(t);

        ensure_arc_length_table();
        t = std::clamp(t, 0.0f, 1.0f);

        const f32 target_length = t * m_total_arc_length;
        auto it = std::lower_bound(m_arc_lengths.begin(), m_arc_lengths.end(), target_length);
        if (it == m_arc_lengths.begin()) return m_samples[0];
        if (it == m_arc_lengths.end()) return m_samples.back();

        const size_t hi = static_cast<size_t>(std::distance(m_arc_lengths.begin(), it));
        const size_t lo = hi - 1;

        const f32 seg_len = m_arc_lengths[hi] - m_arc_lengths[lo];
        const f32 frac = (seg_len > 1e-6f) ? (target_length - m_arc_lengths[lo]) / seg_len : 0.0f;

        return m_samples[lo] + (m_samples[hi] - m_samples[lo]) * frac;
    }

    [[nodiscard]] f32 total_arc_length() const {
        ensure_arc_length_table();
        return m_total_arc_length;
    }

    // ── Internal helpers ─────────────────────────────────────────────────────

private:
    // Effective boundary: Closed only valid with ≥ 3 unique points.
    [[nodiscard]] CatmullRomBoundary effective_boundary() const {
        if (m_boundary == CatmullRomBoundary::Closed && m_waypoints.size() < 3) {
            return CatmullRomBoundary::Clamped;
        }
        return m_boundary;
    }

    // Compute the parameter t_i for each waypoint (knot values).
    void ensure_parameterization() const {
        if (!m_param_dirty) return;
        m_t_values.clear();
        if (m_waypoints.empty()) { m_param_dirty = false; return; }

        const std::size_t n = m_waypoints.size();
        m_t_values.resize(n);
        m_t_values[0] = 0.0f;
        for (std::size_t i = 1; i < n; ++i) {
            const f32 d = glm::length(m_waypoints[i].position - m_waypoints[i - 1].position);
            f32 dt;
            switch (m_alpha) {
                case CatmullRomAlpha::Uniform:     dt = 1.0f;          break;
                case CatmullRomAlpha::Centripetal: dt = std::sqrt(d);   break;
                case CatmullRomAlpha::Chordal:     dt = d;             break;
            }
            // Floor to a small positive value to avoid degenerate segments.
            if (dt < 1e-5f) dt = 1e-5f;
            m_t_values[i] = m_t_values[i - 1] + dt;
        }
        m_total_t = m_t_values.back();
        m_param_dirty = false;
    }

    // Resolve the 4 control points P0, P1, P2, P3 for the segment containing
    // global parameter value `t`. P1 and P2 bracket t.
    void resolve_segment_open(f32 t, Vec3 out[4], f32 out_local_t[2]) const {
        ensure_parameterization();
        const std::size_t n = m_waypoints.size();
        if (n < 2) {
            for (int i = 0; i < 4; ++i) out[i] = (n == 1) ? m_waypoints[0].position : Vec3{0.0f};
            out_local_t[0] = 0.0f; out_local_t[1] = 0.0f;
            return;
        }

        // Map t ∈ [0,1] to global parameter t_g ∈ [0, total_t]
        const f32 t_g = t * m_total_t;

        // Find segment [P_i, P_{i+1}) such that m_t_values[i] <= t_g < m_t_values[i+1]
        std::size_t i = 0;
        for (std::size_t k = 0; k + 1 < n; ++k) {
            if (t_g >= m_t_values[k] && t_g <= m_t_values[k + 1]) { i = k; break; }
            if (k + 1 == n - 1) i = k;
        }
        // Edge case: t_g == total_t
        if (t_g >= m_t_values[n - 1]) i = n - 2;

        const Vec3& P1 = m_waypoints[i].position;
        const Vec3& P2 = m_waypoints[i + 1].position;

        // Phantom endpoints
        Vec3 P0, P3;
        if (i == 0) {
            switch (m_boundary) {
                case CatmullRomBoundary::Clamped: {
                    const Vec3 dir = P1 - P2;
                    P0 = P1 + dir;
                    break;
                }
                case CatmullRomBoundary::Open:
                default:
                    P0 = P1;
                    break;
                case CatmullRomBoundary::Closed:
                    P0 = m_waypoints[n - 1].position; // never reached in open path
                    break;
            }
        } else {
            P0 = m_waypoints[i - 1].position;
        }

        if (i + 2 < n) {
            P3 = m_waypoints[i + 2].position;
        } else {
            switch (m_boundary) {
                case CatmullRomBoundary::Clamped: {
                    const Vec3 dir = P2 - P1;
                    P3 = P2 + dir;
                    break;
                }
                case CatmullRomBoundary::Open:
                default:
                    P3 = P2;
                    break;
                case CatmullRomBoundary::Closed:
                    P3 = m_waypoints[0].position;
                    break;
            }
        }

        out[0] = P0; out[1] = P1; out[2] = P2; out[3] = P3;

        // Local parameter u ∈ [0,1] for the segment.
        // u = (t_g - t0) / (t1 - t0) — linear mapping of global to local
        // parameter. The 4-point Catmull-Rom polynomial below is then
        // evaluated in u, so the knot spacing (uniform/centripetal/chordal)
        // controls the speed at which the curve traverses each segment.
        const f32 t0 = m_t_values[i];
        const f32 t1 = m_t_values[i + 1];
        out_local_t[0] = t0;
        out_local_t[1] = t1;
    }

    // Compute the segment index and local u for a global t in OPEN boundary mode.
    [[nodiscard]] Vec3 eval_segment_open(f32 t) const {
        Vec3 cp[4];
        f32 ts[2];
        resolve_segment_open(t, cp, ts);
        const f32 t0 = ts[0];
        const f32 t1 = ts[1];
        const f32 t_g = t * m_total_t;
        f32 u = (t1 > t0) ? (t_g - t0) / (t1 - t0) : 0.0f;
        u = std::clamp(u, 0.0f, 1.0f);
        return catmull_rom(cp[0], cp[1], cp[2], cp[3], u);
    }

    [[nodiscard]] Vec3 tangent_segment_open(f32 t) const {
        Vec3 cp[4];
        f32 ts[2];
        resolve_segment_open(t, cp, ts);
        const f32 t0 = ts[0];
        const f32 t1 = ts[1];
        const f32 t_g = t * m_total_t;
        f32 u = (t1 > t0) ? (t_g - t0) / (t1 - t0) : 0.0f;
        u = std::clamp(u, 0.0f, 1.0f);
        // dP/du (the local derivative) — chain-rule scale by (t1 - t0)/m_total_t
        Vec3 dP_du = catmull_rom_derivative(cp[0], cp[1], cp[2], cp[3], u);
        const f32 scale = (m_total_t > 0.0f) ? (t1 - t0) / m_total_t : 1.0f;
        return dP_du * scale;
    }

    // Closed-loop evaluation: wrap the waypoint index modulo n.
    [[nodiscard]] Vec3 eval_segment_closed(f32 t) const {
        ensure_parameterization();
        const std::size_t n = m_waypoints.size();
        // Closed loop: t=1 wraps back to t=0.
        if (m_total_t <= 0.0f) return m_waypoints[0].position;
        f32 t_g = t * m_total_t;
        if (t_g >= m_total_t) t_g = 0.0f;  // wrap t=1 to t=0
        // Find segment [m_t_values[k], m_t_values[k+1]) containing t_g.
        std::size_t i = 0;
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t next = (k + 1) % n;
            // For the wrap-around segment (k = n-1), extend next's value by total_t.
            const f32 next_t = (next > k) ? m_t_values[next] : (m_t_values[next] + m_total_t);
            if (t_g >= m_t_values[k] && t_g <= next_t) { i = k; break; }
        }
        const std::size_t i_next = (i + 1) % n;
        const std::size_t i_prev = (i + n - 1) % n;
        const std::size_t i_next2 = (i + 2) % n;

        const Vec3 cp[4] = {
            m_waypoints[i_prev].position,
            m_waypoints[i].position,
            m_waypoints[i_next].position,
            m_waypoints[i_next2].position,
        };
        const f32 t0 = m_t_values[i];
        const f32 t1 = (i_next > i) ? m_t_values[i_next] : m_t_values[i_next] + m_total_t;
        f32 u = (t1 > t0) ? (t_g - t0) / (t1 - t0) : 0.0f;
        u = std::clamp(u, 0.0f, 1.0f);
        return catmull_rom(cp[0], cp[1], cp[2], cp[3], u);
    }

    [[nodiscard]] Vec3 tangent_segment_closed(f32 t) const {
        ensure_parameterization();
        const std::size_t n = m_waypoints.size();
        if (m_total_t <= 0.0f) return Vec3{0.0f, 0.0f, 0.0f};
        f32 t_g = t * m_total_t;
        if (t_g >= m_total_t) t_g = 0.0f;
        std::size_t i = 0;
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t next = (k + 1) % n;
            const f32 next_t = (next > k) ? m_t_values[next] : (m_t_values[next] + m_total_t);
            if (t_g >= m_t_values[k] && t_g <= next_t) { i = k; break; }
        }
        const std::size_t i_next = (i + 1) % n;
        const std::size_t i_prev = (i + n - 1) % n;
        const std::size_t i_next2 = (i + 2) % n;

        const Vec3 cp[4] = {
            m_waypoints[i_prev].position,
            m_waypoints[i].position,
            m_waypoints[i_next].position,
            m_waypoints[i_next2].position,
        };
        const f32 t0 = m_t_values[i];
        const f32 t1 = (i_next > i) ? m_t_values[i_next] : m_t_values[i_next] + m_total_t;
        f32 u = (t1 > t0) ? (t_g - t0) / (t1 - t0) : 0.0f;
        u = std::clamp(u, 0.0f, 1.0f);
        Vec3 dP_du = catmull_rom_derivative(cp[0], cp[1], cp[2], cp[3], u);
        const f32 scale = (m_total_t > 0.0f) ? (t1 - t0) / m_total_t : 1.0f;
        return dP_du * scale;
    }

    // Standard Catmull-Rom evaluation: 0.5 * (2P1 + (-P0+P2) t + (2P0-5P1+4P2-P3) t^2 + (-P0+3P1-3P2+P3) t^3)
    static Vec3 catmull_rom(const Vec3& P0, const Vec3& P1, const Vec3& P2, const Vec3& P3, f32 t) {
        const f32 t2 = t * t;
        const f32 t3 = t2 * t;
        return 0.5f * (
            (2.0f * P1) +
            (-P0 + P2) * t +
            (2.0f * P0 - 5.0f * P1 + 4.0f * P2 - P3) * t2 +
            (-P0 + 3.0f * P1 - 3.0f * P2 + P3) * t3
        );
    }

    // First derivative of the standard Catmull-Rom form.
    static Vec3 catmull_rom_derivative(const Vec3& P0, const Vec3& P1, const Vec3& P2, const Vec3& P3, f32 t) {
        const f32 t2 = t * t;
        return 0.5f * (
            (-P0 + P2) +
            2.0f * (2.0f * P0 - 5.0f * P1 + 4.0f * P2 - P3) * t +
            3.0f * (-P0 + 3.0f * P1 - 3.0f * P2 + P3) * t2
        );
    }

    void ensure_arc_length_table() const {
        if (!m_arc_length_dirty) return;

        constexpr int kSamples = 256;
        m_samples.resize(kSamples + 1);
        m_arc_lengths.resize(kSamples + 1);

        m_arc_lengths[0] = 0.0f;
        m_samples[0] = evaluate(0.0f);

        for (int i = 1; i <= kSamples; ++i) {
            const f32 t = static_cast<f32>(i) / static_cast<f32>(kSamples);
            m_samples[i] = evaluate(t);
            m_arc_lengths[i] = m_arc_lengths[i - 1]
                             + glm::length(m_samples[i] - m_samples[i - 1]);
        }
        m_total_arc_length = m_arc_lengths[kSamples];
        m_arc_length_dirty = false;
    }

    std::vector<CatmullRomWaypoint> m_waypoints;
    CatmullRomBoundary m_boundary{CatmullRomBoundary::Clamped};
    CatmullRomAlpha   m_alpha{CatmullRomAlpha::Centripetal};

    // Parameterization (knot values), lazily computed.
    mutable std::vector<f32> m_t_values;
    mutable f32 m_total_t{0.0f};
    mutable bool m_param_dirty{true};

    // Arc-length LUT.
    mutable std::vector<Vec3> m_samples;
    mutable std::vector<f32> m_arc_lengths;
    mutable f32 m_total_arc_length{0.0f};
    mutable bool m_arc_length_dirty{true};
};

// ── CatmullRomCameraMotion — drives a Camera2_5D along a Catmull-Rom path ─────
//
// Drop-in complement to CameraMotionPath. Use this when you want the camera to
// pass *through* each waypoint exactly (interpolating spline), as opposed to
// the Bezier version where you tune handles and the curve only approximates.

struct CatmullRomCameraMotion {
    CatmullRomPath3D path;
    AutoOrientMode   auto_orient{AutoOrientMode::None};
    Vec3             point_of_interest{0.0f, 0.0f, 0.0f};
    EasingCurve      easing{Easing::Linear};
    f32              zoom{1000.0f};
    f32              fov_deg{50.0f};
    Camera2_5DProjectionMode projection_mode{Camera2_5DProjectionMode::Zoom};
    f32              roll_deg{0.0f};
    bool             use_arc_length{false};

    [[nodiscard]] Camera2_5D evaluate(Frame frame, Frame start, Frame end) const {
        Camera2_5D cam;
        cam.enabled = true;

        if (path.empty() || end <= start) {
            cam.position = path.empty() ? Vec3{0.0f, 0.0f, -1000.0f} : path.evaluate(0.0f);
            cam.zoom = zoom;
            cam.fov_deg = fov_deg;
            cam.projection_mode = projection_mode;
            return cam;
        }

        const f32 raw_t = static_cast<f32>(frame - start) / static_cast<f32>(end - start);
        const f32 t = std::clamp(easing.apply(std::clamp(raw_t, 0.0f, 1.0f)), 0.0f, 1.0f);

        cam.position = use_arc_length ? path.evaluate_arc_length(t) : path.evaluate(t);
        cam.zoom = zoom;
        cam.fov_deg = fov_deg;
        cam.projection_mode = projection_mode;

        switch (auto_orient) {
            case AutoOrientMode::None:
                break;
            case AutoOrientMode::AlongPath: {
                Vec3 forward = path.forward_at(t);
                if (glm::length(forward) > 1e-4f) {
                    f32 yaw   = glm::degrees(std::atan2(forward.x, -forward.z));
                    f32 pitch = glm::degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));
                    cam.rotation = Vec3{pitch, yaw, roll_deg};
                    cam.point_of_interest = cam.position + forward * 1000.0f;
                    cam.point_of_interest_enabled = true;
                }
                break;
            }
            case AutoOrientMode::TowardsPOI: {
                cam.point_of_interest = point_of_interest;
                cam.point_of_interest_enabled = true;
                cam.rotation = Vec3{0.0f, 0.0f, roll_deg};
                break;
            }
        }
        return cam;
    }

    [[nodiscard]] Camera2_5D evaluate(f32 t) const {
        Camera2_5D cam;
        cam.enabled = true;
        if (path.empty()) {
            cam.position = Vec3{0.0f, 0.0f, -1000.0f};
            cam.zoom = zoom;
            cam.fov_deg = fov_deg;
            cam.projection_mode = projection_mode;
            return cam;
        }
        t = std::clamp(t, 0.0f, 1.0f);
        cam.position = use_arc_length ? path.evaluate_arc_length(t) : path.evaluate(t);
        cam.zoom = zoom;
        cam.fov_deg = fov_deg;
        cam.projection_mode = projection_mode;

        switch (auto_orient) {
            case AutoOrientMode::None: break;
            case AutoOrientMode::AlongPath: {
                Vec3 forward = path.forward_at(t);
                if (glm::length(forward) > 1e-4f) {
                    f32 yaw   = glm::degrees(std::atan2(forward.x, -forward.z));
                    f32 pitch = glm::degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));
                    cam.rotation = Vec3{pitch, yaw, roll_deg};
                    cam.point_of_interest = cam.position + forward * 1000.0f;
                    cam.point_of_interest_enabled = true;
                }
                break;
            }
            case AutoOrientMode::TowardsPOI:
                cam.point_of_interest = point_of_interest;
                cam.point_of_interest_enabled = true;
                cam.rotation = Vec3{0.0f, 0.0f, roll_deg};
                break;
        }
        return cam;
    }
};

} // namespace chronon3d

#pragma once

#include <chronon3d/animation/easing.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/core/types/types.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace chronon3d {

// ── SpatialBezierPath — 3D cubic bezier path for camera motion ───────────────
//
// A path made of cubic bezier segments in 3D space. Each waypoint has a
// position and two tangent handles (in/out). The path is evaluated by
// normalizing t ∈ [0,1] across all segments.
//
// Usage:
//   SpatialBezierPath path;
//   path.add_waypoint({0, 0, -1200}, {200, 0, 0}, {-100, 50, 0});
//   path.add_waypoint({200, 50, -800}, {-50, -30, 0}, {0, 0, 0});
//   path.add_waypoint({0, 0, -600}, {100, 0, 0}, {-100, 0, 0});
//   Vec3 pos = path.evaluate(0.5f);
//   Vec3 tan = path.tangent_at(0.5f);

struct Waypoint {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 in_handle{0.0f, 0.0f, 0.0f};   // offset from position (incoming tangent)
    Vec3 out_handle{0.0f, 0.0f, 0.0f};  // offset from position (outgoing tangent)

    constexpr Waypoint() = default;
    constexpr Waypoint(Vec3 pos, Vec3 out_h, Vec3 in_h)
        : position(pos), in_handle(in_h), out_handle(out_h) {}
};

class SpatialBezierPath {
public:
    SpatialBezierPath() = default;

    // ── Fluent builder ───────────────────────────────────────────────────────

    SpatialBezierPath& add_waypoint(Vec3 position, Vec3 out_handle = {}, Vec3 in_handle = {}) {
        m_waypoints.push_back({position, out_handle, in_handle});
        m_arc_length_dirty = true;
        return *this;
    }

    // ── Evaluation ───────────────────────────────────────────────────────────
    // Evaluate position at normalized t ∈ [0, 1] across the entire path.

    [[nodiscard]] Vec3 evaluate(f32 t) const {
        if (m_waypoints.empty()) return Vec3{0.0f};
        if (m_waypoints.size() == 1) return m_waypoints[0].position;

        t = std::clamp(t, 0.0f, 1.0f);

        const size_t num_segments = m_waypoints.size() - 1;
        const f32 segment_t = t * static_cast<f32>(num_segments);
        const size_t seg_idx = std::min(static_cast<size_t>(segment_t), num_segments - 1);
        const f32 local_t = segment_t - static_cast<f32>(seg_idx);

        return eval_cubic_bezier(seg_idx, local_t);
    }

    // Evaluate the tangent (first derivative / velocity) at normalized t.
    [[nodiscard]] Vec3 tangent_at(f32 t) const {
        if (m_waypoints.size() < 2) return Vec3{0.0f};

        t = std::clamp(t, 0.0f, 1.0f);

        const size_t num_segments = m_waypoints.size() - 1;
        const f32 segment_t = t * static_cast<f32>(num_segments);
        const size_t seg_idx = std::min(static_cast<size_t>(segment_t), num_segments - 1);
        const f32 local_t = segment_t - static_cast<f32>(seg_idx);

        return eval_cubic_tangent(seg_idx, local_t);
    }

    // Evaluate the normalized forward direction at t.
    [[nodiscard]] Vec3 forward_at(f32 t) const {
        Vec3 tan = tangent_at(t);
        f32 len = glm::length(tan);
        return (len > 1e-6f) ? (tan / len) : Vec3{0.0f, 0.0f, -1.0f};
    }

    // ── Arc-length parameterization ──────────────────────────────────────────
    // Evaluate with arc-length parameterization for uniform speed along the path.
    // This samples the path at high resolution and uses a lookup table.

    [[nodiscard]] Vec3 evaluate_arc_length(f32 t) const {
        if (m_waypoints.size() < 2) return evaluate(t);

        ensure_arc_length_table();

        t = std::clamp(t, 0.0f, 1.0f);

        // Find the two entries in the LUT that bracket this arc-length fraction
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

    // ── Queries ──────────────────────────────────────────────────────────────

    [[nodiscard]] std::size_t waypoint_count() const { return m_waypoints.size(); }
    [[nodiscard]] std::size_t segment_count() const {
        return (m_waypoints.size() >= 2) ? (m_waypoints.size() - 1) : 0;
    }
    [[nodiscard]] bool empty() const { return m_waypoints.empty(); }

    [[nodiscard]] const std::vector<Waypoint>& waypoints() const { return m_waypoints; }
    [[nodiscard]] std::vector<Waypoint>& waypoints() { return m_waypoints; }

    [[nodiscard]] f32 total_arc_length() const {
        ensure_arc_length_table();
        return m_total_arc_length;
    }

    void clear() {
        m_waypoints.clear();
        m_samples.clear();
        m_arc_lengths.clear();
        m_total_arc_length = 0.0f;
        m_arc_length_dirty = true;
    }

private:
    // Evaluate cubic bezier position for segment i at local t ∈ [0, 1]
    [[nodiscard]] Vec3 eval_cubic_bezier(size_t seg, f32 t) const {
        const Vec3 p0 = m_waypoints[seg].position;
        const Vec3 p1 = p0 + m_waypoints[seg].out_handle;
        const Vec3 p3 = m_waypoints[seg + 1].position;
        const Vec3 p2 = p3 + m_waypoints[seg + 1].in_handle;

        const f32 u = 1.0f - t;
        return u * u * u * p0
             + 3.0f * u * u * t * p1
             + 3.0f * u * t * t * p2
             + t * t * t * p3;
    }

    // Evaluate cubic bezier tangent (first derivative) for segment i at local t
    [[nodiscard]] Vec3 eval_cubic_tangent(size_t seg, f32 t) const {
        const Vec3 p0 = m_waypoints[seg].position;
        const Vec3 p1 = p0 + m_waypoints[seg].out_handle;
        const Vec3 p3 = m_waypoints[seg + 1].position;
        const Vec3 p2 = p3 + m_waypoints[seg + 1].in_handle;

        const f32 u = 1.0f - t;
        return 3.0f * u * u * (p1 - p0)
             + 6.0f * u * t * (p2 - p1)
             + 3.0f * t * t * (p3 - p2);
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
            m_arc_lengths[i] = m_arc_lengths[i - 1] + glm::length(m_samples[i] - m_samples[i - 1]);
        }

        m_total_arc_length = m_arc_lengths[kSamples];
        m_arc_length_dirty = false;
    }

    std::vector<Waypoint> m_waypoints;

    // Arc-length LUT (mutable for lazy computation)
    mutable std::vector<Vec3> m_samples;
    mutable std::vector<f32> m_arc_lengths;
    mutable f32 m_total_arc_length{0.0f};
    mutable bool m_arc_length_dirty{true};
};

// ── AutoOrientationMode — how camera orientation is computed ─────────────────

enum class AutoOrientMode {
    None,          // no auto-orientation, use default rotation
    AlongPath,     // camera looks in direction of movement
    TowardsPOI     // camera always looks towards a fixed point of interest
};

// ── CameraMotionPath — lets CameraRig follow a 3D bezier path ───────────────
//
// Usage:
//   CameraMotionPath motion;
//   motion.path.add_waypoint({0,0,-1200}, {200,0,0}, {-100,50,0});
//   motion.path.add_waypoint({200,50,-800}, {0,0,0}, {0,0,0});
//   motion.auto_orient = AutoOrientMode::AlongPath;
//   motion.easing = EasingCurve{Easing::InOutCubic};
//
//   Camera2_5D cam = motion.evaluate(Frame{45}, Frame{0}, Frame{90});

struct CameraMotionPath {
    SpatialBezierPath path;
    AutoOrientMode auto_orient{AutoOrientMode::None};
    Vec3 point_of_interest{0.0f, 0.0f, 0.0f};
    EasingCurve easing{Easing::Linear};
    f32 zoom{1000.0f};
    f32 fov_deg{50.0f};
    Camera2_5DProjectionMode projection_mode{Camera2_5DProjectionMode::Zoom};
    f32 roll_deg{0.0f};  // constant roll applied after auto-orientation

    // Use arc-length parameterization for uniform speed
    bool use_arc_length{false};

    // Evaluate camera at a given frame within the motion range.
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

        // Compute normalized t with easing
        const f32 raw_t = static_cast<f32>(frame - start) / static_cast<f32>(end - start);
        const f32 t = std::clamp(easing.apply(std::clamp(raw_t, 0.0f, 1.0f)), 0.0f, 1.0f);

        // Evaluate position along path
        cam.position = use_arc_length ? path.evaluate_arc_length(t) : path.evaluate(t);
        cam.zoom = zoom;
        cam.fov_deg = fov_deg;
        cam.projection_mode = projection_mode;

        // Auto-orientation
        switch (auto_orient) {
            case AutoOrientMode::None:
                break;

            case AutoOrientMode::AlongPath: {
                Vec3 forward = path.forward_at(t);
                // Compute rotation from forward direction
                // In our coordinate system: forward is -Z, up is +Y
                if (glm::length(forward) > 1e-4f) {
                    f32 yaw = glm::degrees(std::atan2(forward.x, -forward.z));
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

    // Convenience: evaluate with default start/end
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
            case AutoOrientMode::None:
                break;
            case AutoOrientMode::AlongPath: {
                Vec3 forward = path.forward_at(t);
                if (glm::length(forward) > 1e-4f) {
                    f32 yaw = glm::degrees(std::atan2(forward.x, -forward.z));
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

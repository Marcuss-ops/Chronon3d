#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_trajectory.hpp
//
// Data model for a 3D camera trajectory. Each primitive has its own segment
// type (Linear / CubicBezier / CatmullRom / Hold). The whole trajectory is a
// sequence of segments. Arc-length parameterization is optional but recommended
// for "cinematic-speed-uniform" feel.
//
// No allocations in the hot path (sample() takes ctx only).
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_motion_context.hpp>
#include <chronon3d/scene/camera/camera_v1/arc_length_table.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>  // Camera2_5D

#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <cstddef>

namespace chronon3d::camera_v1 {

namespace chronon3d::camera_v1 {

enum class SegmentKind : std::uint8_t { Linear = 0, CubicBezier = 1, CatmullRom = 2, Hold = 3 };

/// One control point of a trajectory.
struct CameraTrajectoryPoint {
    Vec3 position;
    Vec3 handle_in{0,0,0};   ///< Local offset; used by cubic-bezier / catmull-rom.
    Vec3 handle_out{0,0,0};  ///< Local offset.
    std::optional<Vec3> target;  ///< If set, overrides base_target for this point.
    float roll_deg{0.0f};
};

/// A single segment connecting two trajectory points.
struct CameraTrajectorySegment {
    SegmentKind kind{SegmentKind::Linear};
    std::size_t from_idx{0};
    std::size_t to_idx{0};
    float duration_frames{30.0f};   ///< Total frames for this segment.
};

/// Sample output: position + tangent + (optional) target override.
struct CameraTrajectorySample {
    Vec3 position;
    Vec3 tangent;
    std::optional<Vec3> target;
    float roll_deg{0.0f};
};

/// The full trajectory is immutable once built.
class CameraTrajectory {
public:
    using SampleFn = CameraTrajectorySample (*)(const CameraTrajectory&,
                                               const CameraTrajectorySegment&,
                                               float t01);

    /// Evaluate at integer frame `frame` from the start of the trajectory.
    /// `t01` is replaced by an arc-length remap if pre_lut is provided.
    CameraTrajectorySample sample(const CameraMotionContext& ctx) const;

    /// Total duration in frames (sum of segment durations).
    float total_frames() const;

    /// Number of segments.
    std::size_t size() const { return segments_.size(); }

    /// Read-only access for diagnostics.
    const std::vector<CameraTrajectoryPoint>& points() const { return points_; }
    const std::vector<CameraTrajectorySegment>& segments() const { return segments_; }
    bool arc_length_parameterized() const { return pre_lut_ != nullptr; }

private:
    friend class CameraTrajectoryBuilder;
    std::vector<CameraTrajectoryPoint> points_;
    std::vector<CameraTrajectorySegment> segments_;
    std::shared_ptr<ArcLengthTable> pre_lut_;  // nullopt = uniform t01
};

// =========================================================================
// Fluent builder.
// =========================================================================
class CameraTrajectoryBuilder {
public:
    /// Append a new control point. Returns *this.
    CameraTrajectoryBuilder& move_to(Vec3 p, std::optional<Vec3> target = std::nullopt, float roll_deg = 0.0f);

    /// Append a cubic-bezier point. The handles are in absolute world space
    /// (not offsets), avoiding ambiguity at the boundary between segments.
    CameraTrajectoryBuilder& bezier_to(Vec3 handle_in, Vec3 handle_out, Vec3 to);

    /// Append a catmull-rom point. Tangents are computed inside the segment.
    CameraTrajectoryBuilder& catmull_rom_to(Vec3 to);

    /// Append a hold segment (camera stays put for `frames`).
    CameraTrajectoryBuilder& hold_for(float frames);

    /// Set duration of the LAST appended segment (default 30f).
    CameraTrajectoryBuilder& duration_frames(float f);

    /// Enable arc-length parameterization (pre-computed LUT).
    CameraTrajectoryBuilder& arc_length_parameterized(bool on = true);

    /// Build the immutable trajectory.
    std::shared_ptr<CameraTrajectory> build();

private:
    std::vector<CameraTrajectoryPoint> pts_;
    std::vector<CameraTrajectorySegment> segs_;
    bool arc_len_{false};
};

} // namespace chronon3d::camera_v1

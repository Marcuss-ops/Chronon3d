// ==============================================================================
// chronon3d/src/scene/camera/camera_path_sampler.cpp
//
// P5 hardening:
//   - max_jerk computed and checked against opts.max_jerk
//   - max_velocity_jump checked against opts.max_velocity_jump
//   - point_of_interest_enabled stored in CameraPathSample (no re-evaluation)
//   - analyze_camera_path() as the canonical entry-point
//   - sample_camera_path() remains as legacy wrapper
// ==============================================================================
#include <chronon3d/scene/camera/camera_path_sampler.hpp>
#include <chronon3d/scene/camera/camera_path_validation.hpp>
#include <chronon3d/scene/camera/camera_projection.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <limits>
#include <string>

namespace chronon3d {

// =========================================================================
// analyze_camera_path — canonical entry-point (P5 hardening).
// =========================================================================

CameraPathReport analyze_camera_path(
    const CameraRig& rig,
    const ResolvedSceneTransforms& transforms,
    Viewport viewport,
    SampleRange range,
    CameraPathValidationOptions opts)
{
    CameraPathReport report;
    if (range.step <= 0) range.step = 1;

    // --- Phase 1: sample every frame, store POI enabled flag. ---
    for (int frame = range.start_frame; frame <= range.end_frame; frame += range.step) {
        Camera2_5D cam = rig.evaluate(frame, &transforms);
        CameraPathSample sample;
        sample.frame = frame;
        sample.position = cam.position;
        sample.target = cam.point_of_interest_enabled ? cam.point_of_interest : Vec3{0.0f, 0.0f, 0.0f};
        sample.point_of_interest_enabled = cam.point_of_interest_enabled;  // store once

        if (cam.point_of_interest_enabled && glm::length(cam.point_of_interest - cam.position) > 0.001f) {
            sample.forward = glm::normalize(cam.point_of_interest - cam.position);
        } else {
            Quat rot = cam.rotation_quaternion();
            sample.forward = rot * Vec3{0.0f, 0.0f, 1.0f};
        }

        ScreenPoint sp = project_world_to_screen(sample.target, cam, viewport);
        if (sp.behind_camera) {
            sample.target_center_error_px = std::numeric_limits<float>::max();
        } else {
            sample.target_center_error_px = glm::distance(
                sp.position, Vec2{viewport.width * 0.5f, viewport.height * 0.5f});
        }
        report.max_target_center_error_px = std::max(
            report.max_target_center_error_px, sample.target_center_error_px);

        report.samples.push_back(sample);
    }

    // --- Phase 2: velocity, acceleration, jerk. ---
    if (report.samples.size() >= 2) {
        std::vector<Vec3> velocities;
        velocities.reserve(report.samples.size() - 1);
        for (size_t i = 0; i + 1 < report.samples.size(); ++i) {
            float dt = static_cast<float>(report.samples[i+1].frame - report.samples[i].frame);
            if (dt <= 0.0f) dt = 1.0f;
            velocities.push_back((report.samples[i+1].position - report.samples[i].position) / dt);
        }

        std::vector<Vec3> accelerations;
        if (velocities.size() >= 2) {
            accelerations.reserve(velocities.size() - 1);
            for (size_t i = 0; i + 1 < velocities.size(); ++i) {
                float dt = static_cast<float>(report.samples[i+2].frame - report.samples[i+1].frame);
                if (dt <= 0.0f) dt = 1.0f;
                accelerations.push_back((velocities[i+1] - velocities[i]) / dt);
            }
        }

        // Velocity jumps.
        for (size_t i = 0; i + 1 < velocities.size(); ++i) {
            report.max_velocity_jump = std::max(
                report.max_velocity_jump,
                glm::distance(velocities[i+1], velocities[i]));
        }

        // Acceleration jumps.
        for (size_t i = 0; i + 1 < accelerations.size(); ++i) {
            report.max_acceleration_jump = std::max(
                report.max_acceleration_jump,
                glm::distance(accelerations[i+1], accelerations[i]));
        }

        // Jerk = Δacceleration / dt.
        if (accelerations.size() >= 2) {
            for (size_t i = 0; i + 1 < accelerations.size(); ++i) {
                float dt = static_cast<float>(
                    report.samples[i+3].frame - report.samples[i+2].frame);
                if (dt <= 0.0f) dt = 1.0f;
                report.max_jerk = std::max(
                    report.max_jerk,
                    glm::distance(accelerations[i+1], accelerations[i]) / dt);
            }
        }
    }

    // --- Phase 3: validation against opts. ---
    if (report.max_target_center_error_px > opts.max_target_center_error_px) {
        report.passed = false;
        report.failures.push_back(
            "Max target center error (" + std::to_string(report.max_target_center_error_px) +
            " px) exceeds " + std::to_string(opts.max_target_center_error_px) + " px limit.");
    }

    if (report.max_velocity_jump > opts.max_velocity_jump) {
        report.passed = false;
        report.failures.push_back(
            "Max velocity jump (" + std::to_string(report.max_velocity_jump) +
            ") exceeds " + std::to_string(opts.max_velocity_jump) + " limit.");
    }

    if (report.max_acceleration_jump > opts.max_acceleration_jump) {
        report.passed = false;
        report.failures.push_back(
            "Max acceleration jump (" + std::to_string(report.max_acceleration_jump) +
            ") exceeds " + std::to_string(opts.max_acceleration_jump) + " — jerky movement.");
    }

    if (report.max_jerk > opts.max_jerk) {
        report.passed = false;
        report.failures.push_back(
            "Max jerk (" + std::to_string(report.max_jerk) +
            ") exceeds " + std::to_string(opts.max_jerk) + " limit.");
    }

    // POI check using the stored sample field (no re-evaluation).
    if (opts.require_point_of_interest) {
        for (const auto& s : report.samples) {
            if (!s.point_of_interest_enabled) {
                report.passed = false;
                report.failures.push_back(
                    "Frame " + std::to_string(s.frame) +
                    ": require_point_of_interest failed (POI disabled).");
                break;
            }
        }
    }

    // Ensure the end frame is always sampled (diagnostic).
    if (report.samples.empty() ||
        report.samples.back().frame < range.end_frame) {
        Camera2_5D cam = rig.evaluate(range.end_frame, &transforms);
        CameraPathSample last;
        last.frame = range.end_frame;
        last.position = cam.position;
        last.target = cam.point_of_interest_enabled ? cam.point_of_interest : Vec3{0,0,0};
        last.point_of_interest_enabled = cam.point_of_interest_enabled;
        report.samples.push_back(last);
    }

    return report;
}

// =========================================================================
// sample_camera_path — legacy wrapper (unchanged ABI).
// =========================================================================

CameraPathReport sample_camera_path(
    const CameraRig& rig,
    const ResolvedSceneTransforms& transforms,
    Viewport viewport,
    int start_frame,
    int end_frame,
    const CameraPathValidationOptions& opts,
    int step)
{
    SampleRange range{start_frame, end_frame, step};
    return analyze_camera_path(rig, transforms, viewport, range, opts);
}

CameraPathReport sample_camera_path(
    const CameraRig& rig,
    const ResolvedSceneTransforms& transforms,
    Viewport viewport,
    int start_frame,
    int end_frame,
    int step)
{
    return sample_camera_path(
        rig, transforms, viewport,
        start_frame, end_frame,
        CameraPathValidationOptions{}, step);
}

} // namespace chronon3d

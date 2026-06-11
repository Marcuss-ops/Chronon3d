#include <chronon3d/scene/camera/camera_path_sampler.hpp>
#include <chronon3d/scene/camera/camera_projection.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <limits>

namespace chronon3d {

CameraPathReport sample_camera_path(
    const CameraRig& rig,
    const TransformResolverResult& transforms,
    Viewport viewport,
    int start_frame,
    int end_frame,
    int step
) {
    CameraPathReport report;
    if (step <= 0) step = 1;

    for (int frame = start_frame; frame <= end_frame; frame += step) {
        Camera2_5D cam = rig.evaluate(frame, &transforms);
        CameraPathSample sample;
        sample.frame = frame;
        sample.position = cam.position;
        sample.target = cam.point_of_interest_enabled ? cam.point_of_interest : Vec3{0.0f, 0.0f, 0.0f};
        
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
            sample.target_center_error_px = glm::distance(sp.position, Vec2{viewport.width * 0.5f, viewport.height * 0.5f});
        }
        report.max_target_center_error_px = std::max(report.max_target_center_error_px, sample.target_center_error_px);

        report.samples.push_back(sample);
    }

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

        for (size_t i = 0; i + 1 < velocities.size(); ++i) {
            report.max_velocity_jump = std::max(report.max_velocity_jump, glm::distance(velocities[i+1], velocities[i]));
        }

        for (size_t i = 0; i + 1 < accelerations.size(); ++i) {
            report.max_acceleration_jump = std::max(report.max_acceleration_jump, glm::distance(accelerations[i+1], accelerations[i]));
        }
    }

    if (report.max_target_center_error_px > 5.0f) {
        report.passed = false;
        report.failures.push_back("Max target center error (" + std::to_string(report.max_target_center_error_px) + " px) exceeds 5.0 px limit.");
    }
    if (report.max_acceleration_jump > 15.0f) {
        report.passed = false;
        report.failures.push_back("Max acceleration jump (" + std::to_string(report.max_acceleration_jump) + ") indicates jerky movement.");
    }

    return report;
}

} // namespace chronon3d

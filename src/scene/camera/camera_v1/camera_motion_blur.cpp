// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/camera_motion_blur.cpp
//
// CameraMotionBlurIntegrator implementation.
//
// Sub-sample distribution:
//   exposure_duration = shutter_angle / 360 * (1 / fps)
//   window_start = frame - exposure_duration * (0.5 + shutter_phase / 360)
//   With shutter_phase = -90° (default), window centers on frame.
//
// Jitter:
//   Uniform:   sub-sample at center of each stratum
//   Stratified: random offset within each stratum (seeded LCG)
//   Halton:    low-discrepancy Halton(2,3) sequence offset
//
// Filter weights:
//   Box:      1/N (uniform)
//   Triangle: linear ramp, peak at center, zero at edges
//   Gaussian:  exp(-0.5 * ((t_norm - 0.5) / sigma)^2), sigma = 0.25
//
// Accumulation:
//   position:     weighted average of vec3
//   rotation:     quaternion accumulation + normalize (NLerp-like average)
//   poi, zoom, fov, focus_distance: weighted average of scalars
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/camera_motion_blur.hpp>
#include <chronon3d/math/camera_pose.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace chronon3d::camera_v1 {

// =========================================================================
// Halton sequence
// =========================================================================

float CameraMotionBlurIntegrator::halton(unsigned i, unsigned b) {
    float f = 0.0f;
    float b_inv = 1.0f / static_cast<float>(b);
    float factor = b_inv;
    while (i > 0) {
        f += static_cast<float>(i % b) * factor;
        i /= b;
        factor *= b_inv;
    }
    return f;
}

// =========================================================================
// Sub-sample times
// =========================================================================

std::vector<SampleTime> CameraMotionBlurIntegrator::sub_sample_times(
        double frame, FrameRate frame_rate, int n) const {
    std::vector<SampleTime> times;
    times.reserve(static_cast<std::size_t>(n));

    double fps = frame_rate.fps();
    if (fps <= 0.0) fps = 30.0;
    double frame_duration = 1.0 / fps;

    // Shutter window width.
    double exposure = frame_duration *
        (static_cast<double>(settings_.shutter_angle_deg) / 360.0);

    // Phase shift: -90° centres exposure around frame (default).
    double phase_shift = frame_duration *
        (static_cast<double>(settings_.shutter_phase_deg) / 360.0);

    // Window: [frame + phase_shift, frame + phase_shift + exposure].
    // With 180° / -90°: [frame - 0.25fd, frame + 0.25fd] — centered.
    double window_start = frame + phase_shift;

    // LCG for stratified jitter (deterministic from seed).
    auto lcg_rand = [s = settings_.jitter_seed]() mutable -> float {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<float>((s >> 33) & 0x3FFFFFFF) / static_cast<float>(0x3FFFFFFF);
    };

    for (int i = 0; i < n; ++i) {
        double offset;
        switch (settings_.pattern) {
        case TemporalSamplePattern::Uniform:
            offset = (static_cast<double>(i) + 0.5) / static_cast<double>(n);
            break;
        case TemporalSamplePattern::Stratified:
            offset = (static_cast<double>(i) + static_cast<double>(lcg_rand())) / static_cast<double>(n);
            break;
        case TemporalSamplePattern::Halton:
            offset = static_cast<double>(halton(static_cast<unsigned>(i + 1), 2));
            break;
        }

        double sub_frame = window_start + offset * exposure;
        times.push_back(SampleTime::from_frame(sub_frame, frame_rate));
    }

    return times;
}

// =========================================================================
// Filter weights
// =========================================================================

float CameraMotionBlurIntegrator::filter_weight(float t_norm, int n) const {
    switch (settings_.filter) {
    case TemporalFilter::Box:
        return 1.0f / static_cast<float>(n);

    case TemporalFilter::Triangle: {
        // Triangle: peak at center (t=0.5), zero at edges (t=0, t=1).
        float d = std::abs(t_norm - 0.5f) * 2.0f;  // d ∈ [0, 1]
        return 1.0f - d;  // linear ramp
    }

    case TemporalFilter::Gaussian: {
        // sigma = 0.25 → bell centered at t_norm=0.5.
        constexpr float sigma = 0.25f;
        float z = (t_norm - 0.5f) / sigma;
        return std::exp(-0.5f * z * z);
    }
    }
    return 1.0f / static_cast<float>(n);
}

// =========================================================================
// evaluate()
// =========================================================================

Camera2_5D CameraMotionBlurIntegrator::evaluate(
        double frame, FrameRate frame_rate,
        const CameraEvaluatorFn& evaluator) const {

    // If motion blur disabled or single sample, evaluate at frame center.
    if (!settings_.enabled || settings_.samples <= 1) {
        SampleTime st = SampleTime::from_frame(frame, frame_rate);
        return evaluator(st);
    }

    int n = settings_.samples;
    auto times = sub_sample_times(frame, frame_rate, n);

    // Accumulate.
    Vec3  acc_pos{0, 0, 0};
    Vec3  acc_poi{0, 0, 0};
    Quat  acc_rot_sum{0, 0, 0, 0};
    float acc_zoom = 0.0f;
    float acc_fov  = 0.0f;
    float acc_focus = 0.0f;
    float weight_sum = 0.0f;
    bool  first_quat = true;

    // Compute shutter window start for t_norm derivation.
    double fps = frame_rate.fps();
    if (fps <= 0.0) fps = 30.0;
    double frame_duration = 1.0 / fps;
    double exposure = frame_duration *
        (static_cast<double>(settings_.shutter_angle_deg) / 360.0);
    double phase_shift = frame_duration *
        (static_cast<double>(settings_.shutter_phase_deg) / 360.0);
    double window_start = frame + phase_shift;

    for (int i = 0; i < n; ++i) {
        Camera2_5D sub_cam = evaluator(times[i]);

        // Normalized time within shutter window from actual sub-sample position.
        float t_norm = static_cast<float>(
            (times[i].frame - window_start) / std::max(1e-9, exposure));
        t_norm = std::clamp(t_norm, 0.0f, 1.0f);
        float w = filter_weight(t_norm, n);
        weight_sum += w;

        // Position, POI: weighted average.
        acc_pos += sub_cam.position * w;
        if (sub_cam.point_of_interest_enabled) {
            acc_poi += sub_cam.point_of_interest * w;
        }

        // Rotation: quaternion accumulation (NLerp average).
        // Use separate qw to avoid corrupting scalar accumulators with flipped weight.
        Quat q = math::camera_rotation_quat(sub_cam.rotation);
        float qw = w;
        if (first_quat) {
            acc_rot_sum = q * qw;
            first_quat = false;
        } else {
            // Ensure shortest arc: if dot < 0, flip sign of quaternion contribution.
            if (glm::dot(acc_rot_sum, q) < 0.0f) qw = -qw;
            acc_rot_sum += q * qw;
        }

        acc_zoom  += sub_cam.zoom * w;
        acc_fov   += sub_cam.fov_deg * w;
        acc_focus += sub_cam.focus_distance * w;
    }

    // Normalize.
    float inv_w = (weight_sum > 1e-6f) ? (1.0f / weight_sum) : 1.0f;

    Camera2_5D result = evaluator(SampleTime::from_frame(frame, frame_rate));
    result.position = acc_pos * inv_w;
    if (result.point_of_interest_enabled) {
        result.point_of_interest = acc_poi * inv_w;
    }

    // Quaternion: normalize the accumulated sum.
    Quat avg_rot = glm::normalize(acc_rot_sum);
    result.rotation = math::camera_rotation_euler(avg_rot);

    result.zoom           = acc_zoom * inv_w;
    result.fov_deg        = acc_fov * inv_w;
    result.focus_distance = acc_focus * inv_w;

    // Preserve non-accumulated fields from the base camera.
    result.motion_blur = settings_;
    result.is_animated = true;

    return result;
}

} // namespace chronon3d::camera_v1

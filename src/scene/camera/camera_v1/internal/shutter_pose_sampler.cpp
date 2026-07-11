// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/internal/shutter_pose_sampler.cpp
//
// Internal helper used by CameraProgram / CameraDescriptor for shutter-aware
// pose evaluation.  PR1: renamed from `CameraMotionBlurIntegrator` and
// deduplicated against `src/render_graph/pipeline/composition.cpp` —
// sample-time / jitter / filter-weight generation now goes through
// chronon3d::temporal::generate_temporal_samples (single source of truth).
//
// ── TICKET-PROJECTION-V1: motion-blur-no-recompile invariant ──────────────────
//
// The user spec mandates: "La compilazione deve restare FUORI da
// render_frame, tile loop, nodo, layer, motion blur subsample."
// (Compilation must stay OUT of render_frame, tile loop, node, layer,
// motion blur subsample.)
//
// This invariant is satisfied by the following architectural property:
//
//   1. `ShutterPoseSampler` is constructed ONCE per `MotionBlurSettings`,
//      typically at composition-build time or at the start of a render
//      pass.  The constructor reads the `MotionBlurSettings` (mode +
//      samples + shutter_angle_deg + shutter_phase_deg + pattern + filter
//      + jitter_seed) and computes the per-tick Halton weights via
//      `chronon3d::temporal::generate_temporal_samples`.
//
//   2. The sub-frame loop calls `ShutterPoseSampler::evaluate(frame, fps, ...)`
//      which returns an accumulated `Camera2_5D` for the shutter window.
//      The sampler does NOT re-construct itself per call; the temporal
//      weights + Halton sequence are pre-computed and held by value in
//      the sampler (per `params` + `samples` locals below).
//
//   3. The per-tick `evaluator` is a `CameraEvaluatorFn` (function pointer /
//      closure) that the caller pre-binds to a PRE-COMPILED `CameraProgram`.
//      The `evaluator(sub_frame)` call invokes `program.evaluate(ctx, ...)`
//      on the pre-compiled program; it does NOT call `compile_camera()`
//      per tick.  The `CameraProgram` is built once by `compile_camera()`
//      outside the render loop and reused for all N sub-frame samples.
//
// REGRESSION LOCK: a future change that calls `compile_camera()` or
// `apply_projection_spec()` (which mutates `cam.zoom` / `cam.fov_deg`
// / `cam.lens`) inside the sub-frame loop would break this invariant.
// The motion-blur test suite (`tests/scene/camera/test_motion_blur_torture_pr1.cpp`
// + `tests/visual/cinematic_motion/cinematic_motion_tests.cpp`) exercises
// the full shutter sweep and would catch a recompile-per-tick regression
// via timing + memory-allocation signatures.
//
// DO NOT introduce `compile_camera()` calls inside the sub-frame loop
// or any inner loop.  The compile pipeline lives at the shot / scene
// boundary, NOT in the per-pixel render path.  See AGENTS.md §honesty
// for the canonical no-recompile rule.
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/internal/shutter_pose_sampler.hpp>
#include <chronon3d/math/camera_pose.hpp>
#include <chronon3d/animation/temporal/temporal_samples.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::camera_v1::internal {

// ──────────────────────────────────────────────────────────────────────────────
// evaluate(frame, frame_rate, evaluator)
//
//   1. If motion blur disabled or N == 1 → return evaluator(frame).
//   2. Otherwise: ask chronon3d::temporal for the canonical normalised
//      sample set (sample_times ∈ [0,1] + normalised weights).
//   3. Convert each normalised sample to an absolute sub-frame:
//         sub_frame = frame + (window_start_norm + t * exposure_norm)
//                                  / fps   // back to frame units
//      then evaluator(sub_frame) for each.
//   4. Accumulate position/zoom/fov/focus_distance via weighted sum,
//      rotation via shortest-arc quaternion accumulation + normalize.
//   5. Re-attach settings + is_animated=true to the result.
// ──────────────────────────────────────────────────────────────────────────────
Camera2_5D ShutterPoseSampler::evaluate(
    double frame,
    FrameRate frame_rate,
    const CameraEvaluatorFn& evaluator) const
{
    if (!::chronon3d::is_motion_blur_active(settings_) || settings_.samples <= 1) {
        SampleTime st = SampleTime::from_frame(frame, frame_rate);
        return evaluator(st);
    }

    const int N = settings_.samples;

    chronon3d::temporal::TemporalSampleParams params;
    params.shutter_angle_deg = settings_.shutter_angle_deg;
    params.shutter_phase_deg = settings_.shutter_phase_deg;
    params.pattern           = settings_.pattern;
    params.filter            = settings_.filter;
    params.jitter_seed       = settings_.jitter_seed;

    const chronon3d::temporal::TemporalSampleSet samples =
        chronon3d::temporal::generate_temporal_samples(
            params, N, Frame{static_cast<i64>(std::floor(frame))});

    // ── Convert normalised [0,1] sample times → absolute sub-frame ────────
    const double fps = (frame_rate.fps() > 0.0) ? frame_rate.fps() : 30.0;
    const double frame_duration = 1.0 / fps;

    std::vector<SampleTime> times;
    times.reserve(static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) {
        const double t = samples.sample_times[i];
        const double sub_frame_offset_frames =
            (samples.window_start_normalized + t * samples.exposure_normalized)
            * frame_duration;
        const double sub_frame = frame + sub_frame_offset_frames;
        times.push_back(SampleTime::from_frame(sub_frame, frame_rate));
    }

    // ── Accumulate over sub-samples ─────────────────────────────────────
    Vec3  acc_pos{0, 0, 0};
    Vec3  acc_poi{0, 0, 0};
    Quat  acc_rot_sum{0, 0, 0, 0};
    float acc_zoom  = 0.0f;
    float acc_fov   = 0.0f;
    float acc_focus = 0.0f;
    float weight_sum = 0.0f;
    bool  first_quat = true;

    for (int i = 0; i < N; ++i) {
        Camera2_5D sub_cam = evaluator(times[i]);

        float w = samples.normalized_weights[i];
        weight_sum += w;

        acc_pos += sub_cam.position * w;
        if (sub_cam.point_of_interest_enabled) {
            acc_poi += sub_cam.point_of_interest * w;
        }

        // Rotation: NLerp-style shortest-arc accumulation.
        Quat q = math::camera_rotation_quat(sub_cam.rotation);
        float qw = w;
        if (first_quat) {
            acc_rot_sum = q * qw;
            first_quat = false;
        } else {
            if (glm::dot(acc_rot_sum, q) < 0.0f) qw = -qw;
            acc_rot_sum += q * qw;
        }

        acc_zoom  += sub_cam.zoom          * w;
        acc_fov   += sub_cam.fov_deg       * w;
        acc_focus += sub_cam.dof.focus_distance * w;
    }

    const float inv_w = (weight_sum > 1e-6f) ? (1.0f / weight_sum) : 1.0f;

    Camera2_5D result = evaluator(SampleTime::from_frame(frame, frame_rate));
    result.position = acc_pos * inv_w;
    if (result.point_of_interest_enabled) {
        result.point_of_interest = acc_poi * inv_w;
    }
    Quat avg_rot = glm::normalize(acc_rot_sum);
    result.rotation = math::camera_rotation_euler(avg_rot);

    result.zoom                  = acc_zoom  * inv_w;
    result.fov_deg               = acc_fov   * inv_w;
    result.dof.focus_distance    = acc_focus * inv_w;

    result.motion_blur = settings_;
    result.is_animated = true;
    return result;
}

} // namespace chronon3d::camera_v1::internal

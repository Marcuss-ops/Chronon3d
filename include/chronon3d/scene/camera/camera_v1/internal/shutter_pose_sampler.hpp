#pragma once
// ==============================================================================
// chronon3d/include/chronon3d/scene/camera/camera_v1/internal/shutter_pose_sampler.hpp
//
// ShutterPoseSampler — internal helper that turns MotionBlurSettings + a
// SampleTime-input camera evaluator into a single visibility-corrected
// Camera2_5D pose.  This is *pose sub-sampling*, NOT visible framebuffer
// blur — the visible motion-blur streak on screen is produced by the
// compositor calling comp.evaluate(frame, t) for each sub-frame and
// accumulating the resulting framebuffers (see
// `src/render_graph/pipeline/composition.cpp :: render_composition_frame()`).
//
// PR1: Renamed from `CameraMotionBlurIntegrator` and moved to `internal/`
// to make the boundary explicit: this is a pose-side helper used by
// CameraProgram / CameraDescriptor, NOT a public API for application code.
// The public-legacy header `camera_motion_blur.hpp` re-exports the class
// as a typedef `CameraMotionBlurIntegrator = ShutterPoseSampler` so 1.x
// callers compile unchanged.
//
// Sub-sample distribution responsibilities (now leverages
// chronon3d::temporal::generate_temporal_samples):
//   - shutter window:  exposure = shutter_angle / 360 * 1/fps
//   - phase:            window centred on frame when shutter_phase == -90
//   - jitter:           Uniform / Stratified (seeded) / Halton (base 2)
//   - filter weights:   Box (= 1/N) / Triangle / Gaussian
//   - normalisation:    weights pre-normalised to sum 1.0
// ==============================================================================
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>      // Camera2_5D, MotionBlurSettings
#include <functional>

namespace chronon3d::camera_v1::internal {

/// Callback: given a SampleTime, produce a fully evaluated Camera2_5D.
using CameraEvaluatorFn = std::function<Camera2_5D(SampleTime)>;

// ──────────────────────────────────────────────────────────────────────────────
// ShutterPoseSampler
// ──────────────────────────────────────────────────────────────────────────────
class ShutterPoseSampler {
public:
    explicit ShutterPoseSampler(MotionBlurSettings settings = {})
        : settings_(std::move(settings)) {}

    /// Evaluate motion-blurred camera at the given frame.
    /// If settings_.enabled is false or samples <= 1, returns the evaluator
    /// result at the frame center (no sub-sampling).  Otherwise sub-samples
    /// `settings_.samples` poses across the shutter window and returns a
    /// weighted-average Camera2_5D:
    ///   * position/poi/zoom/fov/focus_distance: arithmetic weighted average
    ///   * rotation: quaternion NLerp (shortest-arc-corrected) then normalised
    Camera2_5D evaluate(
        double frame,
        FrameRate frame_rate,
        const CameraEvaluatorFn& evaluator) const;

    [[nodiscard]] const MotionBlurSettings& settings() const noexcept { return settings_; }
    MotionBlurSettings& settings() noexcept { return settings_; }

private:
    MotionBlurSettings settings_;
};

} // namespace chronon3d::camera_v1::internal

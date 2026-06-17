#pragma once
// ==============================================================================
// chronon3d/scene/camera/camera_v1/camera_motion_blur.hpp
//
// CameraMotionBlurIntegrator — sub-sample accumulation for motion blur.
//
// Distributes N temporal samples across the shutter window and calls a camera
// evaluator for each sub-sample. Results are accumulated with configurable
// temporal filter weights (Box, Triangle, Gaussian) and jitter patterns
// (Uniform, Stratified, Halton).
//
// The evaluator is a std::function<Camera2_5D(SampleTime)> — it receives the
// sub-sample time and returns a fully evaluated camera (trajectory, orientation,
// banking, constraints, framing, shot transition — whatever the caller wires).
//
// This ensures every component of the camera pipeline is evaluated per sub-sample.
//
// Usage:
//   CameraMotionBlurIntegrator integrator(cam.motion_blur);
//   auto blurred = integrator.evaluate(frame, frame_rate,
//       [&](SampleTime st) { return my_program.evaluate(ctx, session).camera; });
// ==============================================================================
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/core/types/sample_time.hpp>

#include <functional>
#include <vector>

namespace chronon3d::camera_v1 {

/// Callback: given a SampleTime, produce a fully evaluated Camera2_5D.
using CameraEvaluatorFn = std::function<Camera2_5D(SampleTime)>;

// =========================================================================
// CameraMotionBlurIntegrator
// =========================================================================

class CameraMotionBlurIntegrator {
public:
    explicit CameraMotionBlurIntegrator(MotionBlurSettings settings = {})
        : settings_(std::move(settings)) {}

    /// Evaluate motion-blurred camera at the given frame.
    /// If settings_.enabled is false or samples <= 1, returns the evaluator result
    /// at the frame center (no sub-sampling).
    Camera2_5D evaluate(double frame, FrameRate frame_rate,
                         const CameraEvaluatorFn& evaluator) const;

    /// Access settings.
    const MotionBlurSettings& settings() const { return settings_; }
    MotionBlurSettings& settings() { return settings_; }

private:
    /// Compute sub-sample times across the shutter window.
    /// Returns N sub-sample times in [frame - half_window, frame + half_window].
    std::vector<SampleTime> sub_sample_times(
        double frame, FrameRate frame_rate, int n) const;

    /// Compute temporal filter weight for sub-sample i of N.
    /// t_norm ∈ [0, 1] is the normalized position within the shutter window.
    float filter_weight(float t_norm, int n) const;

    /// Halton sequence value for index i, base b.
    static float halton(unsigned i, unsigned b);

    MotionBlurSettings settings_;
};

} // namespace chronon3d::camera_v1

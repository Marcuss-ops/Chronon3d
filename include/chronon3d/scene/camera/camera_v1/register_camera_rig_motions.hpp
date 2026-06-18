// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/register_camera_rig_motions.hpp
//
// Phase 2 of the camera subsystem mega-PR.
//
// The 7 legacy inline AnimatedCamera2_5D helpers at the bottom of
// include/chronon3d/scene/model/camera/camera_rig.hpp (camera_rig::hero_push_in, …)
// become thin adapters that construct the matching CameraMotion subclass below
// and sample it across kFrames=90 evenly-spaced frames, materialising the result
// into an AnimatedCamera2_5D (preserving that public return shape).
//
// This keeps every existing caller (scene_presets.hpp, saas_intro_premium.hpp,
// test_animated_camera.cpp, test_stabilization.cpp, …) compiling unchanged
// while moving all the actual motion math into the single-source-of-truth
// V1 plugin layer.
// ==============================================================================
#pragma once

#include <chronon3d/scene/camera/camera_v1/camera_motion_descriptor.hpp>
#include <chronon3d/scene/camera/camera_v1/register_camera_v1.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_motion_context.hpp>  // CameraMotionContext
#include <chronon3d/scene/camera/camera_rig_params.hpp>  // param types (breaks circular dep)
#include <chronon3d/scene/camera/animated_camera_2_5d.hpp>
#include <chronon3d/scene/registry/camera_motion_registry.hpp>
#include <chronon3d/core/types/frame.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d::camera_v1 {

// Public: register all 7 camera_rig::* motion presets with default config in
// CameraMotionRegistry. Idempotent.
void register_camera_rig_motions();

// ── 7 rig-motion data classes ──────────────────────────────────────────────

class HeroPushInMotion final : public CameraMotion {
public:
    explicit HeroPushInMotion(chronon3d::camera_rig::HeroPushInParams p) : p_(p) {
        desc_ = {"camera.rig.hero_push_in", "Hero Push-in", "rig_hero", true};
    }
    CameraMotionDescriptor descriptor() const override { return desc_; }
    Camera2_5D evaluate(const CameraMotionContext& ctx) const override;
private:
    chronon3d::camera_rig::HeroPushInParams p_;
    CameraMotionDescriptor desc_;
};

class OrbitYawMotion final : public CameraMotion {
public:
    explicit OrbitYawMotion(chronon3d::camera_rig::OrbitYawParams p) : p_(p) {
        desc_ = {"camera.rig.orbit_yaw", "Orbit Yaw", "rig_orbit", true};
    }
    CameraMotionDescriptor descriptor() const override { return desc_; }
    Camera2_5D evaluate(const CameraMotionContext& ctx) const override;
private:
    chronon3d::camera_rig::OrbitYawParams p_;
    CameraMotionDescriptor desc_;
};

class ParallaxPanMotion final : public CameraMotion {
public:
    explicit ParallaxPanMotion(chronon3d::camera_rig::ParallaxPanParams p) : p_(p) {
        desc_ = {"camera.rig.parallax_pan", "Parallax Pan", "rig_parallax", true};
    }
    CameraMotionDescriptor descriptor() const override { return desc_; }
    Camera2_5D evaluate(const CameraMotionContext& ctx) const override;
private:
    chronon3d::camera_rig::ParallaxPanParams p_;
    CameraMotionDescriptor desc_;
};

class DollyZoomMotion final : public CameraMotion {
public:
    explicit DollyZoomMotion(chronon3d::camera_rig::DollyZoomParams p) : p_(p) {
        desc_ = {"camera.rig.dolly_zoom", "Dolly Zoom", "rig_dolly_zoom", true};
    }
    CameraMotionDescriptor descriptor() const override { return desc_; }
    Camera2_5D evaluate(const CameraMotionContext& ctx) const override;
private:
    chronon3d::camera_rig::DollyZoomParams p_;
    CameraMotionDescriptor desc_;
};

class FocusPullMotion final : public CameraMotion {
public:
    explicit FocusPullMotion(chronon3d::camera_rig::FocusPullParams p) : p_(p) {
        desc_ = {"camera.rig.focus_pull", "Focus Pull", "rig_focus_pull", true};
    }
    CameraMotionDescriptor descriptor() const override { return desc_; }
    Camera2_5D evaluate(const CameraMotionContext& ctx) const override;
private:
    chronon3d::camera_rig::FocusPullParams p_;
    CameraMotionDescriptor desc_;
};

class LowAngleRevealMotion final : public CameraMotion {
public:
    explicit LowAngleRevealMotion(chronon3d::camera_rig::LowAngleRevealParams p) : p_(p) {
        desc_ = {"camera.rig.low_angle_reveal", "Low-Angle Reveal",
                 "rig_low_angle", true};
    }
    CameraMotionDescriptor descriptor() const override { return desc_; }
    Camera2_5D evaluate(const CameraMotionContext& ctx) const override;
private:
    chronon3d::camera_rig::LowAngleRevealParams p_;
    CameraMotionDescriptor desc_;
};

class SubtleFloatMotion final : public CameraMotion {
public:
    explicit SubtleFloatMotion(chronon3d::camera_rig::SubtleFloatParams p) : p_(p) {
        desc_ = {"camera.rig.subtle_float", "Subtle Float", "rig_float", true};
    }
    CameraMotionDescriptor descriptor() const override { return desc_; }
    Camera2_5D evaluate(const CameraMotionContext& ctx) const override;
private:
    chronon3d::camera_rig::SubtleFloatParams p_;
    CameraMotionDescriptor desc_;
};

// ── Materialisation helper ─────────────────────────────────────────────────
//
// Builds an AnimatedCamera2_5D by sampling `motion.evaluate` at `kFrames`
// evenly-spaced frame stops. With kFrames == motion.duration (the default 90),
// frame 0 returns the start pose and frame N returns the end pose — matching
// what existing tests require (frame-0 == key(0), frame-N == key(N)).
//
// NOTE: Frame is a typedef for long int, so .value() accessor does NOT exist.
// Use the variable directly.
template <typename MotionT>
AnimatedCamera2_5D materialise_acam(const MotionT& motion,
                                    Frame start_frame, Frame duration) {
    AnimatedCamera2_5D cam;
    const Frame span = (duration > Frame{0}) ? duration : Frame{1};

    constexpr int kSamples = 90;  // matches HeroPushIn's default 90-frame span
    for (int i = 0; i <= kSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSamples);
        const i32 frame_offset =
            static_cast<i32>(std::round(t * static_cast<f32>(span)));
        const Frame sample_frame = start_frame + frame_offset;
        CameraMotionContext ctx = CameraMotionContext::at(sample_frame);
        const Camera2_5D sampled = motion.evaluate(ctx);
        cam.position.key(sample_frame, sampled.position);
        cam.rotation.key(sample_frame, sampled.rotation);
        cam.point_of_interest.key(sample_frame, sampled.point_of_interest);
        cam.point_of_interest_enabled = sampled.point_of_interest_enabled;
    }
    return cam;
}

} // namespace chronon3d::camera_v1

// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/register_camera_motion_presets.hpp
//
// Phase 1 of the camera subsystem mega-PR (data-driven camera_motion:: presets).
//
// All 12 camera_motion::* functions (5 parametric + 7 convenience) become
// thin delegates that construct the matching CameraMotion subclass below and
// evaluate it via a CameraMotionContext built from the caller's normalized t.
// The legacy inline implementations in src/scene/camera/camera_motion_presets.cpp
// are replaced.
//
// The 7 convenience presets are also registered with default config in the
// CameraMotionRegistry so external queries by id (e.g. CameraCLI, V1 lookups)
// resolve to a canonical instance.
// ==============================================================================
#pragma once

#include <chronon3d/scene/camera/camera_v1/camera_motion_descriptor.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <chronon3d/scene/registry/camera_motion_registry.hpp>
#include <chronon3d/core/types/frame.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d::camera_v1 {

// Public: register all 12 motion presets with default-config instances in
// CameraMotionRegistry. Idempotent and safe to call multiple times. Called once
// via register_camera_v1_builtins() during module init.
void register_camera_motion_presets();

// Convention: legacy camera_motion::* takes normalized t ∈ [0,1] over a
// 90-frame duration (matches AE-style cinematic motion defaults throughout
// Chronon3d). This helper rebuilds a CameraMotionContext from such a t.
// Public so camera_motion_presets.cpp can call it without re-implementing.
inline CameraMotionContext motion_ctx_from_t(float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    const Frame f{
        static_cast<i32>(std::round(t * 90.0f))
    };
    return CameraMotionContext::at(f);
}

// ── Parametric motion classes (constructor copies caller-supplied params) ──

class DollyMotion final : public CameraMotion {
public:
    explicit DollyMotion(chronon3d::camera_motion::DollyParams p) : p_(p) {
        desc_ = {"camera.dolly_in", "Dolly (parametric)", "dolly", true};
    }
    CameraMotionDescriptor descriptor() const override { return desc_; }
    Camera2_5D evaluate(const CameraMotionContext& ctx) const override;
private:
    chronon3d::camera_motion::DollyParams p_;
    CameraMotionDescriptor desc_;
};

class PanMotion final : public CameraMotion {
public:
    explicit PanMotion(chronon3d::camera_motion::PanParams p) : p_(p) {
        desc_ = {"camera.pan", "Pan (parametric)", "pan", true};
    }
    CameraMotionDescriptor descriptor() const override { return desc_; }
    Camera2_5D evaluate(const CameraMotionContext& ctx) const override;
private:
    chronon3d::camera_motion::PanParams p_;
    CameraMotionDescriptor desc_;
};

class TiltRollMotion final : public CameraMotion {
public:
    explicit TiltRollMotion(chronon3d::camera_motion::TiltRollParams p) : p_(p) {
        desc_ = {"camera.tilt_roll", "Tilt/Roll (static pose)", "tilt_roll", false};
    }
    CameraMotionDescriptor descriptor() const override { return desc_; }
    Camera2_5D evaluate(const CameraMotionContext& ctx) const override;
private:
    chronon3d::camera_motion::TiltRollParams p_;
    CameraMotionDescriptor desc_;
};

// NOTE: name must *not* collide with the data-struct OrbitMotion defined in
// camera_descriptor.hpp (same namespace chronon3d::camera_v1). The preset
// class is suffixed with "Preset" to avoid ODR conflicts in unity builds.
class OrbitPresetMotion final : public CameraMotion {
public:
    explicit OrbitPresetMotion(chronon3d::camera_motion::OrbitParams p) : p_(p) {
        desc_ = {"camera.orbit", "Orbit (parametric)", "orbit", true};
    }
    CameraMotionDescriptor descriptor() const override { return desc_; }
    Camera2_5D evaluate(const CameraMotionContext& ctx) const override;
private:
    chronon3d::camera_motion::OrbitParams p_;
    CameraMotionDescriptor desc_;
};

class PushInTiltMotion final : public CameraMotion {
public:
    explicit PushInTiltMotion(chronon3d::camera_motion::PushInTiltParams p) : p_(p) {
        desc_ = {"camera.push_in_tilt", "Push-in + Tilt (parametric)",
                 "push_in_tilt", true};
    }
    CameraMotionDescriptor descriptor() const override { return desc_; }
    Camera2_5D evaluate(const CameraMotionContext& ctx) const override;
private:
    chronon3d::camera_motion::PushInTiltParams p_;
    CameraMotionDescriptor desc_;
};

// ── Convenience preset motion classes (bakes params in constructor) ────────

class DollyRotateMotion final : public CameraMotion {
public:
    explicit DollyRotateMotion(float zoom) : zoom_(zoom) {
        desc_ = {"camera.dolly_rotate", "Dolly + Rotate", "dolly_rotate", true};
    }
    CameraMotionDescriptor descriptor() const override { return desc_; }
    Camera2_5D evaluate(const CameraMotionContext& ctx) const override;
private:
    float zoom_;
    CameraMotionDescriptor desc_;
};

class RollRevealMotion final : public CameraMotion {
public:
    RollRevealMotion(float max_roll_deg, float zoom)
        : max_roll_deg_(max_roll_deg), zoom_(zoom) {
        desc_ = {"camera.roll_reveal", "Roll Reveal", "roll_reveal", true};
    }
    CameraMotionDescriptor descriptor() const override { return desc_; }
    Camera2_5D evaluate(const CameraMotionContext& ctx) const override;
private:
    float max_roll_deg_;
    float zoom_;
    CameraMotionDescriptor desc_;
};

} // namespace chronon3d::camera_v1

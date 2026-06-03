#pragma once

#include <chronon3d/scene/camera/camera_rig.hpp>
#include <string>
#include <utility>

namespace chronon3d {

class CameraRigBuilder {
public:
    explicit CameraRigBuilder(CameraRig& r) : rig(r) {}

    CameraRigBuilder& two_node(std::string target_name) {
        rig.mode = CameraRigMode::TwoNode;
        rig.target_name = std::move(target_name);
        return *this;
    }

    CameraRigBuilder& one_node() {
        rig.mode = CameraRigMode::OneNode;
        return *this;
    }

    CameraRigBuilder& parent(std::string name) {
        rig.parent_name = std::move(name);
        return *this;
    }

    CameraRigBuilder& orbit_yaw(Frame a, f32 va, Frame b, f32 vb, EasingCurve e = {Easing::InOutCubic}) {
        rig.orbit_yaw.key(a, va).key(b, vb, e);
        return *this;
    }

    CameraRigBuilder& orbit_radius(Frame a, f32 va, Frame b, f32 vb, EasingCurve e = {Easing::OutCubic}) {
        rig.orbit_radius.key(a, va).key(b, vb, e);
        return *this;
    }

    CameraRigBuilder& orbit_pitch(Frame a, f32 va, Frame b, f32 vb, EasingCurve e = {Easing::InOutCubic}) {
        rig.orbit_pitch.key(a, va).key(b, vb, e);
        return *this;
    }

    CameraRigBuilder& dolly(Frame a, f32 va, Frame b, f32 vb, EasingCurve e = {Easing::OutCubic}) {
        rig.dolly.key(a, va).key(b, vb, e);
        return *this;
    }

    CameraRigBuilder& roll(Frame a, f32 va, Frame b, f32 vb, EasingCurve e = {Easing::InOutCubic}) {
        rig.roll.key(a, va).key(b, vb, e);
        return *this;
    }

    CameraRigBuilder& fov(f32 degrees) {
        rig.projection_mode = Camera2_5DProjectionMode::Fov;
        rig.fov_deg.set(degrees);
        return *this;
    }

    CameraRigBuilder& zoom(f32 z) {
        rig.projection_mode = Camera2_5DProjectionMode::Zoom;
        rig.zoom.set(z);
        return *this;
    }

    CameraRigBuilder& focus_target(std::string name, f32 aperture = 0.015f, f32 max_blur = 24.0f) {
        rig.dof.enabled = true;
        rig.dof.focus_target_name = std::move(name);
        rig.dof.aperture.set(aperture);
        rig.dof.max_blur.set(max_blur);
        rig.dof.use_target_z = true;
        return *this;
    }

private:
    CameraRig& rig;
};

} // namespace chronon3d

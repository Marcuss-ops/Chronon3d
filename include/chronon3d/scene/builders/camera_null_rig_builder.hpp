#pragma once

#include <chronon3d/animation/animated_value.hpp>
#include <chronon3d/scene/model/camera_null_rig.hpp>
#include <string>
#include <functional>

namespace chronon3d {

// Forward declaration
class SceneBuilder;

// ---------------------------------------------------------------------------
// CameraNullRigBuilder — Fluent builder API for constructing a CameraNullRig.
//
// Usage:
//   scene.camera_null_rig("MyRig", [](CameraNullRigBuilder& rig) {
//       rig
//         .add_null("ctrl_xy")
//             .position().key(0, Vec3{0,0,0}).key(90, Vec3{200,0,0})
//             .rotation().key(0, Vec3{0,0,0})
//             .scale().set(1,1,1)
//           .end_null()
//         .add_null("ctrl_z")
//             .position().key(0, Vec3{0,0,0}).key(90, Vec3{0,0,100})
//           .end_null()
//         .camera_position(Vec3{0, 0, -1000})
//         .zoom(1000)
//         .fov(50);
//   });
// ---------------------------------------------------------------------------

class CameraNullNodeBuilder {
public:
    explicit CameraNullNodeBuilder(CameraNullNode& node)
        : node_(&node) {}

    // Access animated position for keyframing.
    AnimatedValue<Vec3>& position() { return node_->position; }

    // Access animated rotation (Euler degrees) for keyframing.
    AnimatedValue<Vec3>& rotation() { return node_->rotation; }

    // Access animated scale for keyframing.
    AnimatedValue<Vec3>& scale() { return node_->scale; }

    // Set whether this null inherits parent transforms.
    CameraNullNodeBuilder& inherits_position(bool v) {
        node_->inherits_position = v;
        return *this;
    }
    CameraNullNodeBuilder& inherits_rotation(bool v) {
        node_->inherits_rotation = v;
        return *this;
    }
    CameraNullNodeBuilder& inherits_scale(bool v) {
        node_->inherits_scale = v;
        return *this;
    }
    CameraNullNodeBuilder& visible_in_diagnostics(bool v) {
        node_->visible_in_diagnostics = v;
        return *this;
    }

private:
    CameraNullNode* node_;
};

class CameraNullRigBuilder {
public:
    explicit CameraNullRigBuilder(CameraNullRig& rig)
        : rig_(&rig) {}

    // Add a new null node to the chain and return a builder for it.
    CameraNullNodeBuilder add_null(std::string name) {
        return CameraNullNodeBuilder{rig_->add_node(std::move(name))};
    }

    // Set an optional root parent (the first null will be parented to this).
    CameraNullRigBuilder& root_parent(std::string name) {
        rig_->root_parent_name = std::move(name);
        return *this;
    }

    // Point of Interest (Two-Node mode).
    CameraNullRigBuilder& enable_poi(bool v = true) {
        rig_->point_of_interest_enabled = v;
        return *this;
    }
    AnimatedValue<Vec3>& point_of_interest() {
        return rig_->point_of_interest;
    }

    // Camera position offset (relative to the last null in the chain).
    CameraNullRigBuilder& camera_position(Vec3 pos) {
        rig_->camera_position_offset.set(pos);
        return *this;
    }
    AnimatedValue<Vec3>& camera_position_animated() {
        return rig_->camera_position_offset;
    }

    // Camera rotation offset.
    CameraNullRigBuilder& camera_rotation(Vec3 euler_deg) {
        rig_->camera_rotation_offset.set(euler_deg);
        return *this;
    }
    AnimatedValue<Vec3>& camera_rotation_animated() {
        return rig_->camera_rotation_offset;
    }

    // Zoom (perspective strength).
    CameraNullRigBuilder& zoom(f32 z) {
        rig_->zoom.set(z);
        return *this;
    }
    AnimatedValue<f32>& zoom_animated() {
        return rig_->zoom;
    }

    // Field of View (if using Fov projection mode).
    CameraNullRigBuilder& fov(f32 deg) {
        rig_->fov_deg.set(deg);
        return *this;
    }
    AnimatedValue<f32>& fov_animated() {
        return rig_->fov_deg;
    }

    // Projection mode.
    CameraNullRigBuilder& projection_mode(Camera2_5DProjectionMode mode) {
        rig_->projection_mode = mode;
        return *this;
    }

    // Depth of Field.
    CameraNullRigBuilder& dof_enable(f32 focus_z_val, f32 aperture_val = 0.015f, f32 max_blur_val = 24.0f) {
        rig_->dof_enabled.set(true);
        rig_->focus_z.set(focus_z_val);
        rig_->aperture.set(aperture_val);
        rig_->max_blur.set(max_blur_val);
        return *this;
    }
    AnimatedValue<f32>& dof_focus_z() { return rig_->focus_z; }
    AnimatedValue<f32>& dof_aperture() { return rig_->aperture; }
    AnimatedValue<f32>& dof_max_blur() { return rig_->max_blur; }

    // Motion Blur.
    CameraNullRigBuilder& motion_blur_enable(int samples = 8, f32 shutter_angle_deg = 180.0f) {
        rig_->motion_blur_enabled.set(true);
        rig_->motion_blur_samples.set(samples);
        rig_->shutter_angle.set(shutter_angle_deg);
        return *this;
    }

    // Build the rig into the scene.
    void build(SceneBuilder& scene, Frame frame) const {
        rig_->build(scene, frame);
    }

    // Evaluate the rig at a frame.
    [[nodiscard]] Camera2_5D evaluate(Frame frame) const {
        return rig_->evaluate(frame);
    }

private:
    CameraNullRig* rig_;
};

} // namespace chronon3d

#include <chronon3d/scene/model/camera/camera_null_rig.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/null_builder.hpp>
#include <chronon3d/scene/model/shape/transform_3d.hpp>
#include <chronon3d/math/camera_pose.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

namespace chronon3d {

// ===========================================================================
// CameraNullNode::evaluate
// ===========================================================================
Transform3D CameraNullNode::evaluate(Frame frame) const {
    Transform3D result;
    result.position     = position.evaluate(frame);
    result.rotation     = rotation.evaluate(frame);
    result.scale        = scale.evaluate(frame);
    result.inherits_position = inherits_position;
    result.inherits_rotation = inherits_rotation;
    result.inherits_scale    = inherits_scale;
    return result;
}

// ===========================================================================
// CameraNullRig::evaluate
//
// Evaluates the entire null chain at a given frame and returns the final
// Camera2_5D. The null chain accumulates transforms from root to leaf,
// producing a final world-space transform. The camera offset is then
// applied on top of the accumulated null transform.
// ===========================================================================
Camera2_5D CameraNullRig::evaluate(Frame frame) const {
    Camera2_5D cam;
    cam.projection_mode = projection_mode;
    cam.zoom            = zoom.evaluate(frame);
    cam.fov_deg         = fov_deg.evaluate(frame);

    // --- Step 1: Accumulate null chain transforms ---
    // Start with identity. For each null, compose its local transform
    // with the accumulated parent transform.
    Mat4 accumulated = Mat4{1.0f};

    for (const auto& node : nodes) {
        Transform3D local_xform = node.evaluate(frame);
        const Mat4 local_mat = local_xform.to_mat4();
        accumulated = accumulated * local_mat;
    }

    // --- Step 2: Extract position and rotation from accumulated matrix ---
    Vec3 scale_ignore;
    Quat rotation_ignore;
    Vec3 skew_ignore;
    Vec4 perspective_ignore;
    glm::decompose(accumulated, scale_ignore, rotation_ignore,
                   cam.position, skew_ignore, perspective_ignore);

    // --- Step 3: Apply camera offset (position + rotation) ---
    const Vec3 cam_pos_offset   = camera_position_offset.evaluate(frame);
    const Vec3 cam_rot_offset   = camera_rotation_offset.evaluate(frame);

    // Combine: the camera's world position = accumulated null position +
    // the camera offset rotated by the accumulated rotation.
    const Quat acc_rot = glm::quat(glm::radians(
        Vec3{0.0f, 0.0f, 0.0f} // we already extracted rotation above? No...
    ));

    // Actually, let's use the extracted rotation from decomposition.
    // The accumulation above already multiplies matrices, so the
    // position from decompose is the world position of the last null.
    // We add the camera offset rotated by the accumulated rotation.
    Vec3 world_cam_pos = cam.position + (rotation_ignore * cam_pos_offset);

    // Get accumulated rotation euler for the camera rotation offset
    const Vec3 acc_euler = glm::degrees(glm::eulerAngles(rotation_ignore));

    cam.position = world_cam_pos;
    cam.rotation = acc_euler + cam_rot_offset;
    cam.orientation = math::camera_rotation_quat(cam.rotation);
    cam.orientation_valid = true;

    // --- Step 4: Point of Interest (Two-Node mode) ---
    if (point_of_interest_enabled) {
        cam.point_of_interest = point_of_interest.evaluate(frame);
        cam.point_of_interest_enabled = true;
    }

    // --- Step 5: Depth of Field ---
    if (dof_enabled.evaluate(frame)) {
        cam.dof.enabled  = true;
        cam.dof.focus_z  = focus_z.evaluate(frame);
        cam.dof.aperture = aperture.evaluate(frame);
        cam.dof.max_blur = max_blur.evaluate(frame);
    }

    return cam;
}

// ===========================================================================
// CameraNullRig::build
//
// Creates all the null layers in the scene builder with proper parenting
// chain, and sets up the camera at the end.
// ===========================================================================
void CameraNullRig::build(SceneBuilder& scene, Frame frame) const {
    if (nodes.empty()) {
        // No nulls — just apply camera directly.
        const Camera2_5D cam = evaluate(frame);
        scene.camera()
            .set(cam)
            .enable(true);
        return;
    }

    // Build null chain: each null parents to the previous one.
    // The first null optionally parents to root_parent_name.
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        Transform3D xform = node.evaluate(frame);

        // If this is the first null and root_parent_name is set, wire parent
        if (i == 0 && !root_parent_name.empty()) {
            xform.parent_name = root_parent_name;
        } else if (i > 0) {
            // Parent to previous null
            xform.parent_name = nodes[i - 1].name;
        }

        scene.null_layer(node.name, [&](NullBuilder& nb) {
            nb.position(xform.position)
              .rotation(xform.rotation)
              .scale(xform.scale)
              .visible_in_diagnostics(node.visible_in_diagnostics);

            if (!xform.parent_name.empty()) {
                nb.parent(xform.parent_name);
            }
        });
    }

    // Set up camera at the end of the chain.
    const Camera2_5D cam = evaluate(frame);
    scene.camera()
        .set(cam)
        .enable(true);

    // If we have at least one null, parent the camera to the last null.
    if (!nodes.empty()) {
        // The camera position in evaluate() already accounts for the null
        // chain transform, so we need to set the camera position relative
        // to the last null. We extract the relative offset.
        // For simplicity, we set the camera position/rotation on the
        // camera itself, and it inherits the last null's transform.
        // Actually, the Camera2_5D doesn't support transform parent chains
        // directly the way NullParams does. So we compute the relative
        // offset from the last null and set it as absolute camera position/rotation.

        // We re-compute: the last null's world transform, then the camera
        // offset from evaluate() is the relative position.
        const Vec3 cam_offset = camera_position_offset.evaluate(frame);
        const Vec3 cam_rot_off = camera_rotation_offset.evaluate(frame);

        scene.camera()
            .position(cam_offset)
            .rotation(cam_rot_off)
            .zoom(zoom.evaluate(frame))
            .fov(fov_deg.evaluate(frame));
    }
}

} // namespace chronon3d

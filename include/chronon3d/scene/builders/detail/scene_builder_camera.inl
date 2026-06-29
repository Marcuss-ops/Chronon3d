// ── include/chronon3d/scene/builders/detail/scene_builder_camera.inl ───
//
// Phase-3.3 mechanical split.  Out-of-line template definition of
// SceneBuilder::camera_rig<F>(name, fn).  Behaviour preserved
// verbatim — rigs CameraRigBuilder, walks every existing scene_
// layer to build ResolvedSceneTransforms inputs (each with
// inherits_position/rotation/scale = true + LayerKind::Null
// preselection), evaluates via rig.evaluate(current_time_, &resolved),
// and stores the resulting Camera2_5D in scene_.
//
// Implicitly inline (template definition).  No additional `inline`
// keyword needed at the definition site.

#pragma once

#include <utility>   // std::forward

namespace chronon3d {

    template <typename Fn>
    SceneBuilder &SceneBuilder::camera_rig(std::string name, Fn &&fn) {
        CameraRig rig;
        rig.name = std::move(name);
        CameraRigBuilder builder(rig);
        std::forward<Fn>(fn)(builder);

        // Resolve parent/target nulls through the canonical HierarchyResolver
        // pipeline.  ResolvedSceneTransforms is the value type consumed by
        // CameraRig::evaluate(); no legacy bridge required.
        std::vector<SceneTransformInput> inputs;
        inputs.reserve(scene_.layers().size());
        for (const auto& layer : scene_.layers()) {
            Transform3D t3d;
            t3d.position = layer.transform.position;
            t3d.rotation = glm::degrees(glm::eulerAngles(layer.transform.rotation));
            t3d.scale = layer.transform.scale;
            t3d.anchor = layer.transform.anchor;
            t3d.parent_name = std::string(layer.parent_name);
            t3d.inherits_position = true;
            t3d.inherits_rotation = true;
            t3d.inherits_scale = true;
            inputs.push_back(SceneTransformInput{
                std::string(layer.name),
                t3d,
                layer.kind != LayerKind::Null,
                false
            });
        }
        ResolvedSceneTransforms resolved = resolve_scene_transforms(inputs);

        Camera2_5D camera_baked = rig.evaluate(current_time_, &resolved);
        scene_.set_camera_2_5d(camera_baked);
        return *this;
    }

} // namespace chronon3d

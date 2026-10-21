#include "render_plan_compiler_detail.hpp"
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <utility>
namespace chronon3d::render_plan::detail {
void configure_scene_camera_and_lighting(
    chronon3d::SceneBuilder& scene, const RenderPlan& plan, bool has_3d_layer,
    const CameraTrackEvaluator& camera_evaluator,
    const chronon3d::FrameContext& ctx) {
        if (plan.lighting_rig) {
            const auto& source = *plan.lighting_rig;
            chronon3d::rendering::LightingRig rig;
            rig.key_direction = chronon3d::rendering::safe_normalize(Vec3{
                source.key_direction[0], source.key_direction[1], source.key_direction[2]});
            rig.key_color = Color{source.key_color[0], source.key_color[1], source.key_color[2], source.key_color[3]};
            rig.key_intensity = source.key_intensity;
            rig.ambient_color = Color{source.ambient_color[0], source.ambient_color[1], source.ambient_color[2], source.ambient_color[3]};
            rig.ambient_intensity = source.ambient_intensity;
            rig.rim.enabled = source.rim_enabled;
            rig.rim.color = Color{source.rim_color[0], source.rim_color[1], source.rim_color[2], source.rim_color[3]};
            rig.rim.intensity = source.rim_intensity;
            rig.rim.power = source.rim_power;
            scene.apply_lighting_rig(rig);
        }

        // Render-plan text positions are converted to centre-relative
        // layer coordinates by apply_layer_primitives(). Keep the
        // approved GlowBasics camera at that canonical world origin; a
        // second canvas-centre translation here would move the text out
        // of frame.
        if (has_3d_layer && !plan.camera) {
            Camera2_5D camera;
            camera.enabled = true;
            camera.optics_mode = CameraOpticsMode::Zoom;
            camera.position = Vec3{0.0f, 0.0f, -1000.0f};
            camera.zoom = 1000.0f;
            camera.point_of_interest = Vec3{0.0f, 0.0f, 0.0f};
            camera.point_of_interest_enabled = true;
            // The implicit camera has a fixed pose. Layer motion is
            // tracked on the layers themselves and must not keep the
            // camera in the per-frame animated-camera path.
            camera.is_animated = false;
            scene.set_evaluated_camera(std::move(camera));
        }
        // RenderPlan camera samples already carry the authored pose and
        // optics. Resolve their scalar tracks directly into renderer state;
        // do not compile another Camera V1 authoring program.
        if (camera_evaluator) {
            auto evaluated_camera = camera_evaluator(ctx.effective_frame());
            scene.set_evaluated_camera(std::move(evaluated_camera));
        }

}
}

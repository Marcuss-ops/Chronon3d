#pragma once

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>

namespace chronon3d::test {

/// Canonical raw-Scene render entry point for tests. The retired
/// SoftwareRenderer scene/debug wrappers are intentionally not reintroduced.
inline std::shared_ptr<Framebuffer> render_scene_graph(
    SoftwareRenderer& renderer, const Scene& scene, const Camera& camera,
    i32 width, i32 height, float fps) {
    return graph::render_scene_via_graph(
        renderer.backend(), renderer.node_cache(), scene, camera,
        width, height, 0, 0.0f, renderer.render_settings(),
        renderer.composition_registry(), renderer.video_decoder(), fps,
        "scene", &renderer);
}

inline std::shared_ptr<Framebuffer> render_scene_graph(
    SoftwareRenderer& renderer, const Scene& scene,
    const std::optional<Camera2_5D>& camera, i32 width, i32 height, float fps) {
    Scene effective_scene = scene.clone();
    if (camera.has_value()) {
        effective_scene.set_camera_2_5d(*camera);
    }
    return render_scene_graph(renderer, effective_scene, Camera{}, width, height, fps);
}

inline std::string debug_render_scene_graph(
    SoftwareRenderer& renderer, const Scene& scene, const Camera& camera,
    i32 width, i32 height, float fps, Frame frame = 0,
    f32 frame_time = 0.0f) {
    return graph::debug_scene_graph(
        renderer.backend(), renderer.node_cache(), scene, camera,
        width, height, frame, frame_time, renderer.render_settings(),
        renderer.composition_registry(), renderer.video_decoder(), fps);
}

} // namespace chronon3d::test

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/software_compositor.hpp>
#include <chronon3d/backends/software/software_effect_runner.hpp>
#include <chronon3d/backends/software/software_node_dispatcher.hpp>
#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/backends/software/fake_extruded_text_renderer.hpp>

namespace chronon3d {

namespace {

// Internal blending and effects are now handled by SoftwareCompositor and SoftwareEffectRunner.

} // namespace

SoftwareRenderer::SoftwareRenderer()
    : m_software_registry(std::make_unique<renderer::SoftwareRegistry>()) {
    renderer::register_builtin_processors(*m_software_registry);
}

std::unique_ptr<Framebuffer> SoftwareRenderer::render_frame(const Composition& comp,
                                                            Frame frame) {
    return software_internal::render_frame(*this, comp, frame);
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render_scene(const Scene& scene,
                                                            const Camera& camera, i32 width,
                                                            i32 height) {
    return std::shared_ptr<Framebuffer>(
        render_scene_internal(scene, camera, width, height, 0, 0.0f).release());
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render_scene(
    const Scene& scene, const std::optional<Camera2_5D>& camera, i32 width, i32 height) {
    Scene effective_scene = scene;
    if (camera.has_value()) {
        effective_scene.set_camera_2_5d(*camera);
    }

    Camera default_camera;
    return std::shared_ptr<Framebuffer>(
        render_scene_internal(effective_scene, default_camera, width, height, 0, 0.0f).release());
}

std::string SoftwareRenderer::debug_render_graph(const Scene& scene, const Camera& camera,
                                                  i32 width, i32 height, Frame frame,
                                                  f32 frame_time) const {
    return software_internal::debug_render_graph(*this, scene, camera, width, height, frame,
                                                  frame_time);
}

std::unique_ptr<Framebuffer>
SoftwareRenderer::render_scene_internal(const Scene& scene, const Camera& camera, i32 width,
                                        i32 height, Frame frame, f32 frame_time) {
    return software_internal::render_scene_internal(*this, scene, camera, width, height, frame,
                                                    frame_time);
}

void SoftwareRenderer::apply_blur(Framebuffer& fb, f32 radius) {
    SoftwareEffectRunner::apply_blur(fb, radius);
}

void SoftwareRenderer::draw_node(Framebuffer& fb, const RenderNode& node,
                                 const RenderState& state, const Camera& camera, i32 width,
                                 i32 height) {
    SoftwareNodeDispatcher::draw_node(*this, fb, node, state, camera, width, height, software_registry());
}

void SoftwareRenderer::apply_effect_stack(Framebuffer& fb, const EffectStack& stack) {
    SoftwareEffectRunner::apply_effect_stack(fb, stack, software_registry());
}

void SoftwareRenderer::composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode) {
    SoftwareCompositor::composite_layer(dst, src, mode);
}

} // namespace chronon3d

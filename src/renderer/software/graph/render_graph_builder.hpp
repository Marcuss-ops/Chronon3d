#pragma once

#include <chronon3d/renderer/software/software_renderer.hpp>
#include <chronon3d/renderer/software/render_graph.hpp>
#include <chronon3d/scene/scene.hpp>
#include <chronon3d/scene/camera/camera.hpp>

namespace chronon3d {
namespace renderer {

rendergraph::RenderGraph build_software_render_graph(
    const SoftwareRenderer& renderer,
    const Scene& scene,
    const Camera& camera,
    i32 width,
    i32 height,
    Frame frame,
    f32 frame_time);

} // namespace renderer
} // namespace chronon3d

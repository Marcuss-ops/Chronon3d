#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/backends/software/software_registry.hpp>

namespace chronon3d {

class SoftwareRenderer;

class SoftwareNodeDispatcher {
public:
    static void draw_node(SoftwareRenderer& renderer,
                          Framebuffer& fb, 
                          const RenderNode& node,
                          const RenderState& state, 
                          const Camera& camera, 
                          i32 width, i32 height,
                          const renderer::SoftwareRegistry& registry);
};

} // namespace chronon3d

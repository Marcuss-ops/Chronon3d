#include <chronon3d/backends/software/software_node_dispatcher.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <iostream>
#include <cstdio>

namespace chronon3d {

namespace software_internal {
    void draw_node(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
                   const RenderState& state, const Camera& camera, i32 width, i32 height);
}

void SoftwareNodeDispatcher::draw_node(SoftwareRenderer& renderer,
                                      Framebuffer& fb, 
                                      const RenderNode& node,
                                      const RenderState& state, 
                                      const Camera& camera, 
                                      i32 width, i32 height,
                                      const renderer::SoftwareRegistry& registry) {
    auto* processor = registry.get_shape(node.shape.type);
    if (processor) {
        processor->draw(renderer, fb, node, state, camera, width, height);
    } else {
        // No fallback allowed. All shape types must have a registered processor.
        std::cerr << "[SoftwareNodeDispatcher] ERROR: No processor registered for shape type: " 
                  << static_cast<int>(node.shape.type) << " (Node: " << node.name << ")" << std::endl;
    }
}

} // namespace chronon3d

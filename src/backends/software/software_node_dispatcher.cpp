#include <iostream>
#include <chronon3d/backends/software/software_node_dispatcher.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d {

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
        spdlog::error("[SoftwareNodeDispatcher] No processor registered for shape type {} node '{}'",
                      static_cast<int>(node.shape.type),
                      std::string(node.name));
    }
}

} // namespace chronon3d

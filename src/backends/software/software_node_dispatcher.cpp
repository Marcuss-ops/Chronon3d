#include <chronon3d/backends/software/software_node_dispatcher.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

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
    if (auto* processor = registry.get_shape(node.shape.type)) {
        processor->draw(fb, node, state, camera, width, height);
    } else {
        software_internal::draw_node(renderer, fb, node, state, camera, width, height);
    }
}

} // namespace chronon3d

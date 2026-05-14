#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/render_graph/render_node.hpp>
#include <chronon3d/renderer/text/text_renderer.hpp>
#include <chronon3d/scene/camera_2_5d.hpp>

namespace chronon3d {

/**
 * FakeExtrudedTextRenderer
 * 
 * Logic for rendering extruded 3D text using a CPU-based triangulation and projection approach.
 */
class FakeExtrudedTextRenderer {
public:
    void draw(
        Framebuffer& fb,
        const RenderNode& node,
        const RenderState& state,
        const Camera& camera,
        i32 width,
        i32 height,
        TextRenderer& text_renderer
    );
};

} // namespace chronon3d

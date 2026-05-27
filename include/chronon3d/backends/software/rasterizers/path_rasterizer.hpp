#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/camera/camera.hpp>
#include <chronon3d/scene/path.hpp>
#include <chronon3d/math/math_base.hpp>
#include <chronon3d/scene/layer/render_node.hpp>

namespace chronon3d::renderer {

class PathRasterizer {
public:
    static void draw_path(Framebuffer& fb, const RenderNode& node, const RenderState& state,
                          const Camera& camera, i32 width, i32 height);
};

} // namespace chronon3d::renderer

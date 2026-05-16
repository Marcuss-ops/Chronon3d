#pragma once

#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/scene/camera/camera.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/scene/shape.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/math/math_base.hpp>

namespace chronon3d {
    class SoftwareRenderer;
}

namespace chronon3d::renderer {

class ShapeProcessor {
public:
    virtual ~ShapeProcessor() = default;

    virtual void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state, 
                      const Camera& camera, i32 width, i32 height) = 0;
    
    virtual raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) = 0;
    virtual bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) = 0;
};

} // namespace chronon3d::renderer

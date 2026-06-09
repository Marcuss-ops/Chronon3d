#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/math/glm_types.hpp>

namespace chronon3d {
    class SoftwareRenderer;
}

namespace chronon3d::renderer {

// Small safety margin added to raster bounds so AA fringes and fractional
// transforms do not leave stale pixels behind when dirty-rect rendering is on.
inline constexpr f32 kBBoxSafetyPadding = 1.5f;

class ShapeProcessor {
public:
    virtual ~ShapeProcessor() = default;

    virtual void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state, 
                      const Camera& camera, i32 width, i32 height) = 0;
    
    virtual raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) = 0;
    virtual bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) = 0;
};

} // namespace chronon3d::renderer

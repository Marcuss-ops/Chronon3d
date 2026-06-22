#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/backends/software/software_processor_context.hpp>

namespace chronon3d {
    class SoftwareRenderer;
}

namespace chronon3d::renderer {

// Small safety margin added to raster bounds so AA fringes and fractional
// transforms do not leave stale pixels behind when dirty-rect rendering is on.
inline constexpr f32 kBBoxSafetyPadding = 1.5f;

// Extra bbox padding for Text shapes in the general compute_world_bbox.
// SourceNode::predicted_bbox uses this general function (not the
// text-specific SoftwareTextProcessor), and the text rasterizer adds 32px
// of internal padding with a y_offset of -16px, which shifts the image
// 16px outside the nominal box bounds in local space.  The generous 128px
// safety margin (~5 tile rows at default tile size) ensures the tiling /
// dirty-rect system never clips the rasterized output due to rounding or
// matrix discrepancies between predicted_bbox and execute.
inline constexpr f32 kTextBBoxPadding = 128.0f;

class ShapeProcessor {
public:
    virtual ~ShapeProcessor() = default;

    virtual void draw(const SoftwareProcessorContext& rctx, Framebuffer& fb, const RenderNode& node, const RenderState& state, 
                      const Camera& camera, i32 width, i32 height) = 0;
    
    virtual raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) = 0;
    virtual bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) = 0;
};

} // namespace chronon3d::renderer

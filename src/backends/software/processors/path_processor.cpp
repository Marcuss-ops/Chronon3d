#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/rasterizers/path_rasterizer.hpp>
// path_geometry.hpp lives next to its sibling path sub-rasterizer
// sources; use a relative include so the build picks it up regardless
// of the public include/ root.
#include "../rasterizers/path/path_geometry.hpp"
#include <chronon3d/math/path_utils.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d::renderer {

class PathProcessor : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
              const RenderState& state, const Camera& camera, i32 width, i32 height) override {
        PathRasterizer::draw_path(fb, node, state, camera, width, height);
    }

    // PR2: delegate to the unified path_geometry implementation so
    // path flattening reuses the LRU cache and the perspective logic
    // is no longer duplicated.  The detail:: prefix keeps the
    // perspective-aware bbox formula and hit-test implementation
    // outside the ABI surface; the public-facing `ShapeProcessor`
    // -> dispatcher is the only public witness of this code.
    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        if (shape.type() != ShapeType::Path) return {0, 0, 0, 0};
        return detail::compute_path_world_bbox(shape.path(), model, spread);
    }

    bool hit_test(const Shape& shape, Vec2 local_point, f32 spread) override {
        if (shape.type() != ShapeType::Path) return false;
        return detail::hit_test_path(shape.path(), local_point, spread);
    }
};

std::unique_ptr<ShapeProcessor> create_path_processor() {
    return std::make_unique<PathProcessor>();
}

} // namespace chronon3d::renderer

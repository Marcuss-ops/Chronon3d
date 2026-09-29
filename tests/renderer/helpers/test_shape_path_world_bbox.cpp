// ── Unit tests for renderer::compute_world_bbox on path shapes ───────────
//
// A path has no size box: its extent is whatever its flattened commands cover.
// The dispatcher in shape_rasterizer.cpp used to fall through to the shared
// size-box branch, where a path's `size` is absent, so `size` stayed {0,0} and
// the bbox collapsed to a 2*pad square centred on the node origin.  The graph
// executor consumes that bbox as the per-node draw clip, so every authored path
// was silently truncated to that square: a 160 px stroked line rendered as a
// ~20 px blob at the layer position, while its geometry was provably correct.
//
// The assertions below are the exact shape of that failure, so a regression
// fails here instead of in a rendered frame.

#include <doctest/doctest.h>

#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/scene/model/shape/path.hpp>
#include <chronon3d/math/glm_types.hpp>

#include "src/backends/software/rasterizers/shape_rasterizer.hpp"
#include "src/backends/software/rasterizers/shape_rasterizer_helpers.hpp"

using namespace chronon3d;

namespace {

/// A horizontal segment of ±half_extent centred on the node origin, stroked.
Shape make_line_path(f32 half_extent, f32 stroke_width) {
    Shape shape;
    shape.set_type(ShapeType::Path);
    PathShape& path = shape.path();
    path.commands.push_back(PathCommand::move_to({-half_extent, 0.0f}));
    path.commands.push_back(PathCommand::line_to({half_extent, 0.0f}));
    path.stroke.enabled = true;
    path.stroke.width = stroke_width;
    path.stroke.color = Color{1.0f, 1.0f, 1.0f, 1.0f};
    return shape;
}

Mat4 translated(f32 x, f32 y) {
    Mat4 model = Mat4(1.0f);
    model[3][0] = x;
    model[3][1] = y;
    return model;
}

}  // namespace

TEST_CASE("path world bbox covers the authored geometry, not a node-origin square") {
    const Shape shape = make_line_path(80.0f, 20.0f);
    const raster::BBox bbox = renderer::compute_world_bbox(shape, translated(200.0f, 130.0f), 8.0f);

    // A 160 px segment with a 20 px stroke and an 8 px spread reaches
    // 200 ± (80 + 10 + 8) horizontally and 130 ± (10 + 8) vertically.
    CHECK(bbox.x0 <= 102);
    CHECK(bbox.x1 >= 298);
    CHECK(bbox.y0 <= 112);
    CHECK(bbox.y1 >= 148);

    // The degenerate answer was a square: the box must be far wider than tall.
    CHECK((bbox.x1 - bbox.x0) > 3 * (bbox.y1 - bbox.y0));
}

TEST_CASE("path bbox padding tracks the path's own stroke width") {
    const Shape thin = make_line_path(80.0f, 4.0f);
    const Shape thick = make_line_path(80.0f, 40.0f);

    const raster::BBox thin_bbox = renderer::compute_world_bbox(thin, Mat4(1.0f), 0.0f);
    const raster::BBox thick_bbox = renderer::compute_world_bbox(thick, Mat4(1.0f), 0.0f);

    // Half the stroke width pads each side, so the thicker stroke yields the
    // taller box.  Before the fix the helper reported 0 for paths, which is how
    // the size-box branch lost the stroke entirely.
    CHECK((thick_bbox.y1 - thick_bbox.y0) > (thin_bbox.y1 - thin_bbox.y0));
    CHECK(renderer::stroke_width_for_shape(thick) == doctest::Approx(40.0f));
    CHECK(renderer::stroke_width_for_shape(thin) == doctest::Approx(4.0f));
}

TEST_CASE("a path with no commands reports an empty bbox instead of a square") {
    Shape empty;
    empty.set_type(ShapeType::Path);
    empty.path().stroke.enabled = true;
    empty.path().stroke.width = 20.0f;

    const raster::BBox bbox = renderer::compute_world_bbox(empty, translated(200.0f, 130.0f), 8.0f);
    CHECK(bbox.is_empty());
}

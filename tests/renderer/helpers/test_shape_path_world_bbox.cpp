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
#include "src/backends/software/rasterizers/path_rasterizer.hpp"

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

TEST_CASE("software path brush rasterization paints dab endpoints into premultiplied framebuffer") {
    Framebuffer framebuffer(64, 64);
    framebuffer.clear(Color::transparent());

    PathShape path;
    path.commands = {PathCommand::move_to({10.0f, 32.0f}),
                     PathCommand::line_to({54.0f, 32.0f})};
    path.fill.enabled = false;
    path.stroke.enabled = true;
    path.stroke.width = 8.0f;
    path.stroke.color = Color::white();
    path.stroke.brush = graphics::BrushProfile::hard_round();
    path.stroke.brush->size = 10.0f;
    path.stroke.brush->flow = 0.5f; // force deterministic endpoint-inclusive dabs

    renderer::draw_path(framebuffer, path, Mat4(1.0f), Color::white(), nullptr);
    const Color start = framebuffer.get_pixel(10, 32);
    const Color middle = framebuffer.get_pixel(32, 32);
    const Color end = framebuffer.get_pixel(54, 32);
    CHECK(start.a > 0.0f);
    CHECK(middle.a > 0.0f);
    CHECK(end.a > 0.0f);
    CHECK(start.r == doctest::Approx(start.a).epsilon(1.0e-5f));
    CHECK(middle.r == doctest::Approx(middle.a).epsilon(1.0e-5f));
    CHECK(framebuffer.get_pixel(32, 50).a == 0.0f);

    path.stroke.brush->scatter = 1.0f;
    path.stroke.brush->width_jitter = 1.0f;
    const auto dabs = path.stroke.brush->sample_dabs(
        {{20.0f, 20.0f}, {40.0f, 20.0f}}, 20.0f,
        path.stroke.brush->size);
    REQUIRE_FALSE(dabs.empty());
    CHECK(dabs.front().position.x >= 15.0f);
    CHECK(dabs.front().position.x <= 45.0f);
}

TEST_CASE("a path with no commands reports an empty bbox instead of a square") {
    Shape empty;
    empty.set_type(ShapeType::Path);
    empty.path().stroke.enabled = true;
    empty.path().stroke.width = 20.0f;

    const raster::BBox bbox = renderer::compute_world_bbox(empty, translated(200.0f, 130.0f), 8.0f);
    CHECK(bbox.is_empty());
}

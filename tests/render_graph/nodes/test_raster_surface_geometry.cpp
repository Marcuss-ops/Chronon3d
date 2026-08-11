#include <doctest/doctest.h>

#include <src/render_graph/nodes/detail/raster_surface.hpp>
#include <chronon3d/text/text_run_shape.hpp>

#include <memory>

using namespace chronon3d;
using namespace chronon3d::graph::detail;

namespace {

std::shared_ptr<TextRunShape> make_shape() {
    auto shape = std::make_shared<TextRunShape>();
    auto layout = std::make_shared<TextRunLayout>();
    layout->font_size = 40.0f;
    layout->placed.ascent = 32.0f;
    layout->placed.descent = 8.0f;

    PlacedGlyph first;
    first.advance_x = 60.0f;
    first.bbox_x0 = -4.0f;
    first.bbox_y0 = 30.0f;
    first.bbox_x1 = 54.0f;
    first.bbox_y1 = -7.0f;
    layout->placed.glyphs.push_back(first);

    PlacedGlyph second = first;
    second.advance_x = 50.0f;
    second.bbox_x0 = -2.0f;
    second.bbox_x1 = 46.0f;
    layout->placed.glyphs.push_back(second);
    shape->layout = std::move(layout);

    GlyphInstanceState a;
    a.layout_position = {10.0f, 20.0f};
    a.fill = Color::white();
    GlyphInstanceState b = a;
    b.layout_position = {70.0f, 20.0f};
    shape->glyphs = {a, b};
    return shape;
}

} // namespace

TEST_CASE("tight raster surface stores local origin and excludes canvas dimensions") {
    const auto shape = make_shape();
    const auto geometry = compute_tight_text_surface_geometry(*shape);

    REQUIRE(geometry.has_value());
    CHECK(geometry->valid());
    CHECK(geometry->origin.x == doctest::Approx(-2.0f));
    CHECK(geometry->origin.y == doctest::Approx(-18.0f));
    CHECK(geometry->content_size.x == doctest::Approx(126.0f));
    CHECK(geometry->content_size.y == doctest::Approx(53.0f));
    CHECK(geometry->origin.x < geometry->ink_min.x);
    CHECK(geometry->origin.y < geometry->ink_min.y);
    CHECK(geometry->content_size.x < 1920.0f);
    CHECK(geometry->content_size.y < 1080.0f);
    CHECK(geometry->width() == static_cast<i32>(std::ceil(geometry->content_size.x)));
    CHECK(geometry->height() == static_cast<i32>(std::ceil(geometry->content_size.y)));
}

TEST_CASE("tight raster surface is deterministic for the same text bounds") {
    const auto shape = make_shape();
    const auto first = compute_tight_text_surface_geometry(*shape);
    const auto second = compute_tight_text_surface_geometry(*shape);

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->origin.x == doctest::Approx(second->origin.x));
    CHECK(first->origin.y == doctest::Approx(second->origin.y));
    CHECK(first->content_size.x == doctest::Approx(second->content_size.x));
    CHECK(first->content_size.y == doctest::Approx(second->content_size.y));
}

TEST_CASE("tight raster surface rejects empty text") {
    TextRunShape shape;
    CHECK_FALSE(compute_tight_text_surface_geometry(shape).has_value());
}

TEST_CASE("tight raster surface remains safe when animated and placed glyph counts differ") {
    const auto shape = make_shape();
    auto mismatch = *shape;
    mismatch.glyphs.push_back(mismatch.glyphs.back());

    const auto geometry = compute_tight_text_surface_geometry(mismatch);
    REQUIRE(geometry.has_value());
    CHECK(geometry->origin.x == doctest::Approx(-2.0f));
    CHECK(geometry->origin.y == doctest::Approx(-18.0f));
    CHECK(geometry->content_size.x == doctest::Approx(126.0f));
    CHECK(geometry->content_size.y == doctest::Approx(53.0f));
}

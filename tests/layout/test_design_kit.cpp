#include <doctest/doctest.h>

#include <chronon3d/layout/design_kit.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/scene/model/render/render_node_factory.hpp>

#include <memory_resource>
using namespace chronon3d;


TEST_CASE("RichTextLine measures a mixed inline line with text, spacing and symbol") {
    RichTextLine rtl;
    rtl.run("Buttery", Color{1.0f, 0.0f, 0.78f, 1.0f}, 72.0f)
       .space(24.0f)
       .run("Smooth", Color::white(), 72.0f)
       .space(24.0f)
       .star(Color{1.0f, 0.0f, 0.78f, 1.0f}, 42.0f, 12.0f, 8);

    const auto metrics = rtl.measure(nullptr);

    CHECK(metrics.width > 0.0f);
    CHECK(metrics.height >= 84.0f);
    CHECK(metrics.ascent >= 42.0f);
    CHECK(metrics.descent >= 42.0f);
    CHECK(metrics.baseline == doctest::Approx(metrics.ascent));
    CHECK(metrics.has_ink_bounds);
    CHECK(metrics.ink_bounds.x <= 0.0f);
    CHECK(metrics.ink_bounds.z > metrics.ink_bounds.x);
    CHECK(metrics.ink_bounds.w < metrics.ink_bounds.y);
}

TEST_CASE("RenderNodeFactory preserves native stroke on rounded rect and circle") {
    auto* res = std::pmr::get_default_resource();

    RoundedRectParams rounded;
    rounded.size = {930.0f, 120.0f};
    rounded.radius = 60.0f;
    rounded.color = Color{0.03f, 0.01f, 0.05f, 0.14f};
    rounded.stroke.enabled = true;
    rounded.stroke.color = Color{0.78f, 0.48f, 0.88f, 0.28f};
    rounded.stroke.width = 1.0f;
    rounded.stroke.alignment = StrokeAlignment::Center;

    auto pill = RenderNodeFactory::rounded_rect(res, "pill", rounded);
    CHECK(pill.shape.rounded_rect().stroke.enabled);
    CHECK(pill.shape.rounded_rect().stroke.width == doctest::Approx(1.0f));
    CHECK(pill.shape.rounded_rect().stroke.color.a == doctest::Approx(0.28f));

    CircleParams circle;
    circle.radius = 48.0f;
    circle.color = Color{1.0f, 1.0f, 1.0f, 1.0f};
    circle.stroke.enabled = true;
    circle.stroke.color = Color{0.1f, 0.8f, 1.0f, 1.0f};
    circle.stroke.width = 4.0f;
    circle.stroke.alignment = StrokeAlignment::Outside;

    auto badge = RenderNodeFactory::circle(res, "badge", circle);
    CHECK(badge.shape.circle().stroke.enabled);
    CHECK(badge.shape.circle().stroke.alignment == StrokeAlignment::Outside);
}

TEST_CASE("Box model helpers fit content with padding and bounds") {
    const Vec2 fitted = box_fit_content(
        {120.0f, 40.0f},
        BoxPadding{16.0f, 8.0f, 16.0f, 8.0f},
        {200.0f, 80.0f},
        Vec2{260.0f, 120.0f}
    );

    CHECK(fitted.x == doctest::Approx(200.0f));
    CHECK(fitted.y == doctest::Approx(80.0f));
}

TEST_CASE("Shape hash changes when stroke changes") {
    Shape a;
    a.type = ShapeType::RoundedRect;
    a.rounded_rect().size = {930.0f, 120.0f};
    a.rounded_rect().radius = 60.0f;
    a.rounded_rect().stroke.enabled = true;
    a.rounded_rect().stroke.width = 1.0f;

    Shape b = a;
    b.rounded_rect().stroke.width = 2.0f;

    CHECK(chronon3d::graph::hash_shape(a) != chronon3d::graph::hash_shape(b));
}

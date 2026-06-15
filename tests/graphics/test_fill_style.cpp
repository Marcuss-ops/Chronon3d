// ── FillStyle unit tests ──────────────────────────────────────────────
//
// Tests for FillStyle ↔ Fill bridge, StrokeStyle ↔ ShapeStroke bridge,
// and GradientDefinition sampling through FillStyle.
//
// NOTE: chronon3d::GradientStop (fill.hpp, field: .offset)
// and chronon3d::graphics::GradientStop (gradient.hpp, field: .position)
// are distinct types.  Code must qualify explicitly where ambiguity exists.

#include <doctest/doctest.h>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/graphics/gradient.hpp>
#include <chronon3d/scene/model/shape/fill.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/scene/model/shape/path.hpp>
#include <chronon3d/vector/shape_style.hpp>

// Bring in chronon3d (Fill, ShapeStroke, PathStroke, ShapeStyle, etc.)
// chronon3d::graphics is used via qualified names to avoid GradientStop clash.
using namespace chronon3d;

// ── Helpers ──────────────────────────────────────────────────────────

/// Shorthand for graphics gradient stop construction.
static auto gs(f32 pos, Color c) -> chronon3d::graphics::GradientStop {
    return {pos, c};
}

static auto os(f32 pos, f32 op) -> chronon3d::graphics::OpacityStop {
    return {pos, op};
}

// ── FillStyle ────────────────────────────────────────────────────────

TEST_CASE("FillStyle -> solid color bridge") {
    const Color c{0.2f, 0.4f, 0.6f, 0.8f};
    const auto fs = chronon3d::graphics::FillStyle::solid(c);
    CHECK(fs.is_solid());
    CHECK_FALSE(fs.is_gradient());

    const Fill f = fs.to_fill();
    CHECK(f.enabled);
    CHECK(f.type == FillType::Solid);
    CHECK(f.solid.r == doctest::Approx(0.2f));
    CHECK(f.solid.g == doctest::Approx(0.4f));
    CHECK(f.solid.b == doctest::Approx(0.6f));
    CHECK(f.solid.a == doctest::Approx(0.8f));
}

TEST_CASE("FillStyle disabled -> solid color bridge") {
    const auto fs = chronon3d::graphics::FillStyle{false, {0.5f, 0.5f, 0.5f, 1.0f}, std::nullopt};
    const Fill f = fs.to_fill();
    CHECK(f.enabled == false);   // to_fill passes enabled through
    CHECK(f.type == FillType::Solid);
}

TEST_CASE("FillStyle -> linear gradient bridge") {
    namespace gfx = chronon3d::graphics;
    std::vector<gfx::GradientStop> stops = {
        gs(0.0f, {1.0f, 0.0f, 0.0f, 1.0f}),
        gs(1.0f, {0.0f, 0.0f, 1.0f, 1.0f}),
    };
    const auto fs = gfx::FillStyle::linear({0.0f, 0.0f}, {1.0f, 0.0f}, stops);
    REQUIRE(fs.is_gradient());

    const Fill f = fs.to_fill();
    CHECK(f.enabled);
    CHECK(f.type == FillType::LinearGradient);
    CHECK(f.gradient.stops.size() == 2);
    CHECK(f.gradient.stops[0].offset == doctest::Approx(0.0f));
    CHECK(f.gradient.stops[0].color.r == doctest::Approx(1.0f));
    CHECK(f.gradient.stops[1].offset == doctest::Approx(1.0f));
    CHECK(f.gradient.stops[1].color.b == doctest::Approx(1.0f));
    CHECK(f.gradient.from.x == doctest::Approx(0.0f));
    CHECK(f.gradient.to.x == doctest::Approx(1.0f));
}

TEST_CASE("FillStyle -> radial gradient bridge") {
    namespace gfx = chronon3d::graphics;
    std::vector<gfx::GradientStop> stops = {
        gs(0.0f, {0.0f, 1.0f, 0.0f, 1.0f}),
        gs(1.0f, {1.0f, 1.0f, 0.0f, 1.0f}),
    };
    const auto fs = gfx::FillStyle::radial({0.5f, 0.5f}, 0.5f, stops);
    REQUIRE(fs.is_gradient());

    const Fill f = fs.to_fill();
    CHECK(f.enabled);
    CHECK(f.type == FillType::RadialGradient);
    CHECK(f.gradient.stops.size() == 2);
    CHECK(f.gradient.from.x == doctest::Approx(0.5f));
    CHECK(f.gradient.from.y == doctest::Approx(0.5f));
    CHECK(f.gradient.to.x == doctest::Approx(1.0f));  // 0.5 + 0.5
}

TEST_CASE("FillStyle solid -> gradient via to_fill distinction") {
    namespace gfx = chronon3d::graphics;
    const auto solid_fs = gfx::FillStyle::solid({1.0f, 1.0f, 1.0f, 1.0f});
    CHECK(solid_fs.to_fill().type == FillType::Solid);

    std::vector<gfx::GradientStop> stops = {
        gs(0.0f, {0,0,0,1}), gs(1.0f, {1,1,1,1})
    };
    const auto grad_fs = gfx::FillStyle::linear({0,0}, {1,0}, std::move(stops));
    CHECK(grad_fs.to_fill().type == FillType::LinearGradient);
}

TEST_CASE("FillStyle opacity stops blended into colour stops") {
    namespace gfx = chronon3d::graphics;
    std::vector<gfx::GradientStop> c_stops = {
        gs(0.0f, {1.0f, 0.0f, 0.0f, 1.0f}),
        gs(1.0f, {0.0f, 0.0f, 1.0f, 1.0f}),
    };
    std::vector<gfx::OpacityStop> o_stops = {
        os(0.0f, 0.5f),
        os(1.0f, 1.0f),
    };

    gfx::GradientDefinition g = gfx::GradientDefinition::linear(
        {0,0}, {1,0}, std::move(c_stops));
    g.opacity_stops = std::move(o_stops);

    gfx::FillStyle fs{true, {1,1,1,1}, std::move(g)};
    const Fill f = fs.to_fill();

    REQUIRE(f.gradient.stops.size() == 2);
    CHECK(f.gradient.stops[0].color.a == doctest::Approx(0.5f));
    CHECK(f.gradient.stops[1].color.a == doctest::Approx(1.0f));
}


// ── StrokeStyle ──────────────────────────────────────────────────────

TEST_CASE("StrokeStyle -> ShapeStroke bridge") {
    namespace gfx = chronon3d::graphics;
    const auto ss = gfx::StrokeStyle::solid({0.8f, 0.2f, 0.2f, 1.0f}, 3.0f);
    CHECK(ss.is_solid());
    CHECK_FALSE(ss.is_gradient());

    const ShapeStroke s = ss.to_shape_stroke();
    CHECK(s.enabled);
    CHECK(s.color.r == doctest::Approx(0.8f));
    CHECK(s.width == doctest::Approx(3.0f));
    CHECK(s.alignment == StrokeAlignment::Center);
}

TEST_CASE("StrokeStyle -> PathStroke bridge") {
    namespace gfx = chronon3d::graphics;
    const auto ss = gfx::StrokeStyle::solid({1.0f, 1.0f, 0.0f, 0.5f}, 2.5f);
    const PathStroke ps = ss.to_path_stroke();
    CHECK(ps.enabled);
    CHECK(ps.color.r == doctest::Approx(1.0f));
    CHECK(ps.width == doctest::Approx(2.5f));
}

TEST_CASE("StrokeStyle gradient query") {
    namespace gfx = chronon3d::graphics;
    std::vector<gfx::GradientStop> stops = {
        gs(0.0f, {1,0,0,1}), gs(1.0f, {0,0,1,1})
    };
    const auto ss = gfx::StrokeStyle::linear_gradient({0,0}, {1,0}, stops, 4.0f);
    CHECK(ss.is_gradient());
    CHECK(ss.width == doctest::Approx(4.0f));
    CHECK(ss.to_shape_stroke().width == doctest::Approx(4.0f));
}

TEST_CASE("StrokeStyle disabled defaults") {
    namespace gfx = chronon3d::graphics;
    const gfx::StrokeStyle ss;
    CHECK_FALSE(ss.enabled);
    CHECK(ss.width == doctest::Approx(1.0f));
    CHECK(ss.is_solid());

    const ShapeStroke s = ss.to_shape_stroke();
    CHECK_FALSE(s.enabled);
}


// ── ShapeStyle integration ───────────────────────────────────────────

TEST_CASE("ShapeStyle with FillStyle") {
    namespace gfx = chronon3d::graphics;
    ShapeStyle style;
    CHECK(style.fill.is_solid());
    CHECK_FALSE(style.stroke.enabled);
    CHECK(style.opacity == doctest::Approx(1.0f));

    std::vector<gfx::GradientStop> stops = {
        gs(0.0f, {1.0f, 0.0f, 0.0f, 1.0f}),
        gs(1.0f, {0.0f, 0.0f, 1.0f, 1.0f}),
    };
    style.fill = gfx::FillStyle::linear({0.0f, 0.0f}, {0.0f, 1.0f}, std::move(stops));
    CHECK(style.fill.is_gradient());

    const Fill f = style.fill.to_fill();
    CHECK(f.type == FillType::LinearGradient);
}

TEST_CASE("ShapeStyle stroke bridge via path_factories pattern") {
    namespace gfx = chronon3d::graphics;
    ShapeStyle style;
    style.stroke = gfx::StrokeStyle::solid({1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);
    style.opacity = 0.5f;

    const PathStroke ps = style.stroke.to_path_stroke();
    CHECK(ps.enabled);
    CHECK(ps.color.r == doctest::Approx(1.0f));
    CHECK(ps.width == doctest::Approx(2.0f));
}


// ── Gradient sampling through FillStyle ──────────────────────────────

TEST_CASE("FillStyle gradient sampling at endpoints") {
    namespace gfx = chronon3d::graphics;
    std::vector<gfx::GradientStop> stops = {
        gs(0.0f, {1.0f, 0.0f, 0.0f, 1.0f}),
        gs(1.0f, {0.0f, 0.0f, 1.0f, 1.0f}),
    };
    const auto fs = gfx::FillStyle::linear({0, 0}, {1, 0}, stops);

    const Fill f = fs.to_fill();
    REQUIRE(f.gradient.stops.size() == 2);

    // Sample at t=0
    const Color c0 = sample_gradient(f.gradient, 0.0f);
    CHECK(c0.r == doctest::Approx(1.0f));
    CHECK(c0.b == doctest::Approx(0.0f));

    // Sample at t=1
    const Color c1 = sample_gradient(f.gradient, 1.0f);
    CHECK(c1.r == doctest::Approx(0.0f));
    CHECK(c1.b == doctest::Approx(1.0f));

    // Sample at midpoint
    const Color cm = sample_gradient(f.gradient, 0.5f);
    CHECK(cm.r == doctest::Approx(0.5f));
    CHECK(cm.b == doctest::Approx(0.5f));
    CHECK(cm.g == doctest::Approx(0.0f));
}

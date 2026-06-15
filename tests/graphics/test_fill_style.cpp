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

TEST_CASE("FillStyle -> conic gradient bridge") {
    namespace gfx = chronon3d::graphics;
    std::vector<gfx::GradientStop> stops = {
        gs(0.0f, {1.0f, 0.0f, 0.0f, 1.0f}),
        gs(0.5f, {0.0f, 1.0f, 0.0f, 1.0f}),
        gs(1.0f, {0.0f, 0.0f, 1.0f, 1.0f}),
    };
    const auto fs = gfx::FillStyle::conic({0.5f, 0.5f}, 0.0f, stops);
    REQUIRE(fs.is_gradient());

    const Fill f = fs.to_fill();
    CHECK(f.enabled);
    CHECK(f.type == FillType::ConicGradient);
    CHECK(f.gradient.stops.size() == 3);
    CHECK(f.gradient.from.x == doctest::Approx(0.5f));
    CHECK(f.gradient.from.y == doctest::Approx(0.5f));
    // angle_rad=0 → to encodes (center.x + cos(0), center.y + sin(0)) = (1.5, 0.5)
    CHECK(f.gradient.to.x == doctest::Approx(1.5f));
    CHECK(f.gradient.to.y == doctest::Approx(0.5f));
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

TEST_CASE("StrokeStyle -> PathStroke with linear gradient") {
    namespace gfx = chronon3d::graphics;
    std::vector<gfx::GradientStop> stops = {
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    const auto ss = gfx::StrokeStyle::linear_gradient({0.0f, 0.0f}, {1.0f, 0.0f}, stops, 3.0f);
    const PathStroke ps = ss.to_path_stroke();
    CHECK(ps.enabled);
    CHECK(ps.width == doctest::Approx(3.0f));
    // Gradient should be propagated
    REQUIRE(ps.gradient.has_value());
    CHECK(ps.gradient->type == FillType::LinearGradient);
    REQUIRE(ps.gradient->stops.size() == 2);
    CHECK(ps.gradient->stops[0].offset == doctest::Approx(0.0f));
    CHECK(ps.gradient->stops[0].color.r == doctest::Approx(1.0f));
    CHECK(ps.gradient->from.x == doctest::Approx(0.0f));
    CHECK(ps.gradient->to.x == doctest::Approx(1.0f));
}

TEST_CASE("StrokeStyle -> ShapeStroke with radial gradient") {
    namespace gfx = chronon3d::graphics;
    std::vector<gfx::GradientStop> stops = {
        {0.0f, {0.0f, 1.0f, 0.0f, 1.0f}},
        {1.0f, {1.0f, 1.0f, 0.0f, 1.0f}},
    };
    const auto ss = gfx::StrokeStyle::radial_gradient({0.5f, 0.5f}, 0.5f, stops, 2.0f);
    const ShapeStroke s = ss.to_shape_stroke();
    CHECK(s.enabled);
    CHECK(s.width == doctest::Approx(2.0f));
    // Gradient should be propagated
    REQUIRE(s.gradient.has_value());
    CHECK(s.gradient->type == FillType::RadialGradient);
    CHECK(s.gradient->from.x == doctest::Approx(0.5f));
    CHECK(s.gradient->to.x == doctest::Approx(1.0f));  // 0.5 + 0.5
}

TEST_CASE("StrokeStyle -> PathStroke with conic gradient") {
    namespace gfx = chronon3d::graphics;
    std::vector<gfx::GradientStop> stops = {
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    const auto ss = gfx::StrokeStyle::conic_gradient({0.5f, 0.5f}, 1.57f, stops, 4.0f);
    const PathStroke ps = ss.to_path_stroke();
    CHECK(ps.enabled);
    CHECK(ps.width == doctest::Approx(4.0f));
    REQUIRE(ps.gradient.has_value());
    CHECK(ps.gradient->type == FillType::ConicGradient);
    CHECK(ps.gradient->from.x == doctest::Approx(0.5f));
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


// ── FillStyle / Gradient interpolation ──────────────────────────────

TEST_CASE("lerp_gradient_stop — position and colour") {
    namespace gfx = chronon3d::graphics;
    const gfx::GradientStop a{0.0f, {1.0f, 0.0f, 0.0f, 1.0f}};
    const gfx::GradientStop b{1.0f, {0.0f, 0.0f, 1.0f, 1.0f}};

    const auto m = gfx::lerp_gradient_stop(a, b, 0.5f);
    CHECK(m.position == doctest::Approx(0.5f));
    CHECK(m.color.r == doctest::Approx(0.5f));
    CHECK(m.color.b == doctest::Approx(0.5f));
    CHECK(m.color.g == doctest::Approx(0.0f));

    // t=0 → a, t=1 → b
    CHECK(gfx::lerp_gradient_stop(a, b, 0.0f).position == doctest::Approx(0.0f));
    CHECK(gfx::lerp_gradient_stop(a, b, 1.0f).position == doctest::Approx(1.0f));
}

TEST_CASE("lerp_fill_style — solid to solid") {
    namespace gfx = chronon3d::graphics;
    const auto red   = gfx::FillStyle::solid({1.0f, 0.0f, 0.0f, 1.0f});
    const auto blue  = gfx::FillStyle::solid({0.0f, 0.0f, 1.0f, 1.0f});

    const auto m = gfx::lerp_fill_style(red, blue, 0.5f);
    CHECK(m.is_solid());
    CHECK(m.solid_color.r == doctest::Approx(0.5f));
    CHECK(m.solid_color.b == doctest::Approx(0.5f));
    CHECK(m.solid_color.g == doctest::Approx(0.0f));

    // t=0 → pure red
    const auto r0 = gfx::lerp_fill_style(red, blue, 0.0f);
    CHECK(r0.solid_color.r == doctest::Approx(1.0f));
    CHECK(r0.solid_color.b == doctest::Approx(0.0f));

    // t=1 → pure blue
    const auto r1 = gfx::lerp_fill_style(red, blue, 1.0f);
    CHECK(r1.solid_color.r == doctest::Approx(0.0f));
    CHECK(r1.solid_color.b == doctest::Approx(1.0f));
}

TEST_CASE("lerp_fill_style — linear gradient to linear gradient") {
    namespace gfx = chronon3d::graphics;
    std::vector<gfx::GradientStop> stops_a = {
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    std::vector<gfx::GradientStop> stops_b = {
        {0.0f, {0.0f, 1.0f, 0.0f, 1.0f}},
        {1.0f, {1.0f, 1.0f, 0.0f, 1.0f}},
    };
    const auto ga = gfx::FillStyle::linear({0.0f, 0.0f}, {1.0f, 0.0f}, stops_a);
    const auto gb = gfx::FillStyle::linear({0.0f, 0.0f}, {1.0f, 0.0f}, stops_b);

    // Midpoint — colours lerped
    const auto m = gfx::lerp_fill_style(ga, gb, 0.5f);
    REQUIRE(m.is_gradient());
    REQUIRE(m.gradient.has_value());
    const auto& g = *m.gradient;
    // Both input have 2 identical positions (0,1) → merged = 2 stops
    REQUIRE(g.color_stops.size() == 2);
    // Stop 0: lerp(red, green) → (0.5, 0.5, 0, 1)
    CHECK(g.color_stops[0].color.r == doctest::Approx(0.5f));
    CHECK(g.color_stops[0].color.g == doctest::Approx(0.5f));
    // Stop 1: lerp(blue, yellow) → (0.5, 0.5, 0.5, 1)
    CHECK(g.color_stops[1].color.r == doctest::Approx(0.5f));
    CHECK(g.color_stops[1].color.g == doctest::Approx(0.5f));
    CHECK(g.color_stops[1].color.b == doctest::Approx(0.5f));

    // Geometry lerped (same start/end in both, so unchanged)
    CHECK(g.start.x == doctest::Approx(0.0f));
    CHECK(g.end.x   == doctest::Approx(1.0f));
}

TEST_CASE("lerp_fill_style — solid to linear gradient") {
    namespace gfx = chronon3d::graphics;
    const auto solid = gfx::FillStyle::solid({1.0f, 1.0f, 1.0f, 1.0f});
    std::vector<gfx::GradientStop> stops = {
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    const auto grad = gfx::FillStyle::linear({0.0f, 0.5f}, {1.0f, 0.5f}, stops);

    // t=0 → solid (still solid in result since we convert to 1-stop gradient)
    const auto r0 = gfx::lerp_fill_style(solid, grad, 0.0f);
    CHECK(r0.is_gradient());  // solid is converted to 1-stop gradient for lerp
    // t=1 → gradient
    const auto r1 = gfx::lerp_fill_style(solid, grad, 1.0f);
    REQUIRE(r1.is_gradient());
    // Midpoint
    const auto m = gfx::lerp_fill_style(solid, grad, 0.5f);
    REQUIRE(m.is_gradient());
}

TEST_CASE("lerp_fill_style — radial gradient to radial") {
    namespace gfx = chronon3d::graphics;
    std::vector<gfx::GradientStop> stops_a = {
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, {1.0f, 1.0f, 1.0f, 1.0f}},
    };
    std::vector<gfx::GradientStop> stops_b = {
        {0.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
        {1.0f, {1.0f, 1.0f, 1.0f, 1.0f}},
    };
    const auto ra = gfx::FillStyle::radial({0.3f, 0.3f}, 0.4f, stops_a);
    const auto rb = gfx::FillStyle::radial({0.7f, 0.7f}, 0.6f, stops_b);

    const auto m = gfx::lerp_fill_style(ra, rb, 0.5f);
    REQUIRE(m.is_gradient());
    REQUIRE(m.gradient.has_value());
    const auto& g = *m.gradient;
    CHECK(g.center.x == doctest::Approx(0.5f));  // (0.3+0.7)/2
    CHECK(g.center.y == doctest::Approx(0.5f));
    CHECK(g.radius   == doctest::Approx(0.5f));  // (0.4+0.6)/2
}

TEST_CASE("lerp_fill_style — conic gradient to conic") {
    namespace gfx = chronon3d::graphics;
    std::vector<gfx::GradientStop> stops = {
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    const auto ca = gfx::FillStyle::conic({0.5f, 0.5f}, 0.0f, stops);
    const auto cb = gfx::FillStyle::conic({0.5f, 0.5f}, 1.57f, stops);  // ~π/2

    const auto m = gfx::lerp_fill_style(ca, cb, 0.5f);
    REQUIRE(m.is_gradient());
    REQUIRE(m.gradient.has_value());
    CHECK(m.gradient->angle == doctest::Approx(0.785f).epsilon(0.01f));  // ~π/4
}

TEST_CASE("lerp_gradient — opacity stops merged and lerped") {
    namespace gfx = chronon3d::graphics;
    std::vector<gfx::GradientStop> c_stops = {
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    std::vector<gfx::OpacityStop> o_a = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    std::vector<gfx::OpacityStop> o_b = {{0.0f, 1.0f}, {1.0f, 0.0f}};

    gfx::GradientDefinition ga = gfx::GradientDefinition::linear({0,0}, {1,0}, c_stops);
    ga.opacity_stops = o_a;
    gfx::GradientDefinition gb = gfx::GradientDefinition::linear({0,0}, {1,0}, c_stops);
    gb.opacity_stops = o_b;

    const auto m = gfx::lerp_gradient(ga, gb, 0.5f);
    REQUIRE(m.opacity_stops.size() == 2);
    CHECK(m.opacity_stops[0].opacity == doctest::Approx(0.5f));  // (0+1)/2
    CHECK(m.opacity_stops[1].opacity == doctest::Approx(0.5f));  // (1+0)/2
}

// ── Unit tests for stroke_gradient_helpers ──────────────────────────────
//
// Tests for the header-only helper functions:
//   - stroke_has_gradient(const Shape&)
//   - resolve_stroke_gradient_color(const Shape&, Vec2 lp, Vec2 sz)
//
// These are defined as `static inline` in
//   src/backends/software/rasterizers/shape_rasterizer_helpers.hpp
// so we include the source file directly.

#include <doctest/doctest.h>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/scene/model/shape/fill.hpp>
#include <chronon3d/math/color.hpp>
#include "src/backends/software/rasterizers/shape_rasterizer_helpers.hpp"

using namespace chronon3d;
using namespace chronon3d::renderer;

// ── Helpers ─────────────────────────────────────────────────────────────

/// Build a simple red→blue linear gradient fill for a shape stroke.
static GradientFill make_linear_rb_gradient() {
    GradientFill g;
    g.type  = FillType::LinearGradient;
    g.from  = {0.0f, 0.5f};
    g.to    = {1.0f, 0.5f};
    g.stops = {
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},  // red at 0
        {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},  // blue at 1
    };
    return g;
}

/// Build a white→black radial gradient fill for a shape stroke.
static GradientFill make_radial_wb_gradient() {
    GradientFill g;
    g.type  = FillType::RadialGradient;
    g.from  = {0.5f, 0.5f};
    g.to    = {1.0f, 0.5f}; // radius = 0.5
    g.stops = {
        {0.0f, {1.0f, 1.0f, 1.0f, 1.0f}},  // white at centre
        {1.0f, {0.0f, 0.0f, 0.0f, 1.0f}},  // black at edge
    };
    return g;
}

/// Build a Shape with the given type and an optional gradient on the stroke.
static Shape make_shape(ShapeType type, const std::optional<GradientFill>& grad = std::nullopt,
                        f32 stroke_w = 10.0f) {
    Shape s;
    s.type = type;
    ShapeStroke stroke;
    stroke.enabled  = true;
    stroke.width    = stroke_w;
    stroke.color    = {0.5f, 0.5f, 0.5f, 1.0f};
    stroke.gradient = grad;
    switch (type) {
        case ShapeType::Rect:
            s.rect.size   = {200.0f, 200.0f};
            s.rect.stroke = stroke;
            break;
        case ShapeType::RoundedRect:
            s.rounded_rect.size   = {200.0f, 200.0f};
            s.rounded_rect.radius = 10.0f;
            s.rounded_rect.stroke = stroke;
            break;
        case ShapeType::Circle:
            s.circle.radius = 100.0f;
            s.circle.stroke = stroke;
            break;
        default:
            break;
    }
    return s;
}

// ══════════════════════════════════════════════════════════════════════════
// stroke_has_gradient
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("stroke_has_gradient: Rect without gradient returns false") {
    Shape s = make_shape(ShapeType::Rect);
    CHECK_FALSE(stroke_has_gradient(s));
}

TEST_CASE("stroke_has_gradient: Rect with linear gradient returns true") {
    Shape s = make_shape(ShapeType::Rect, make_linear_rb_gradient());
    CHECK(stroke_has_gradient(s));
}

TEST_CASE("stroke_has_gradient: Rect with radial gradient returns true") {
    Shape s = make_shape(ShapeType::Rect, make_radial_wb_gradient());
    CHECK(stroke_has_gradient(s));
}

TEST_CASE("stroke_has_gradient: RoundedRect with gradient returns true") {
    Shape s = make_shape(ShapeType::RoundedRect, make_linear_rb_gradient());
    CHECK(stroke_has_gradient(s));
}

TEST_CASE("stroke_has_gradient: Circle with gradient returns true") {
    Shape s = make_shape(ShapeType::Circle, make_linear_rb_gradient());
    CHECK(stroke_has_gradient(s));
}

TEST_CASE("stroke_has_gradient: RoundedRect without gradient returns false") {
    Shape s = make_shape(ShapeType::RoundedRect);
    CHECK_FALSE(stroke_has_gradient(s));
}

TEST_CASE("stroke_has_gradient: Circle without gradient returns false") {
    Shape s = make_shape(ShapeType::Circle);
    CHECK_FALSE(stroke_has_gradient(s));
}

TEST_CASE("stroke_has_gradient: None type returns false") {
    Shape s;
    s.type = ShapeType::None;
    CHECK_FALSE(stroke_has_gradient(s));
}

TEST_CASE("stroke_has_gradient: disabled stroke with gradient still returns true") {
    // stroke_has_gradient only checks gradient.has_value(), not enabled.
    auto grad = make_linear_rb_gradient();
    Shape s = make_shape(ShapeType::Rect, grad);
    s.rect.stroke.enabled = false;
    CHECK(stroke_has_gradient(s)); // gradient IS present even if disabled
}

TEST_CASE("stroke_has_gradient: Line type returns false") {
    Shape s;
    s.type = ShapeType::Line;
    CHECK_FALSE(stroke_has_gradient(s));
}

TEST_CASE("stroke_has_gradient: Text type returns false") {
    Shape s;
    s.type = ShapeType::Text;
    CHECK_FALSE(stroke_has_gradient(s));
}

// ══════════════════════════════════════════════════════════════════════════
// resolve_stroke_gradient_color — Linear gradient
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("resolve_stroke_gradient_color: linear at t=0 returns first stop (red)") {
    Shape s = make_shape(ShapeType::Rect, make_linear_rb_gradient());
    // lp = left edge → norm.x = 0 → t = 0 → red
    Color c = resolve_stroke_gradient_color(s, {0.0f, 100.0f}, {200.0f, 200.0f});
    Color expected = Color{1.0f, 0.0f, 0.0f, 1.0f}.to_linear();
    CHECK(c.r == doctest::Approx(expected.r).epsilon(1e-5f));
    CHECK(c.g == doctest::Approx(expected.g).epsilon(1e-5f));
    CHECK(c.b == doctest::Approx(expected.b).epsilon(1e-5f));
}

TEST_CASE("resolve_stroke_gradient_color: linear at t=1 returns last stop (blue)") {
    Shape s = make_shape(ShapeType::Rect, make_linear_rb_gradient());
    // lp = right edge → norm.x = 1 → t = 1 → blue
    Color c = resolve_stroke_gradient_color(s, {200.0f, 100.0f}, {200.0f, 200.0f});
    Color expected = Color{0.0f, 0.0f, 1.0f, 1.0f}.to_linear();
    CHECK(c.r == doctest::Approx(expected.r).epsilon(1e-5f));
    CHECK(c.g == doctest::Approx(expected.g).epsilon(1e-5f));
    CHECK(c.b == doctest::Approx(expected.b).epsilon(1e-5f));
}

TEST_CASE("resolve_stroke_gradient_color: linear at t=0.5 interpolates") {
    Shape s = make_shape(ShapeType::Rect, make_linear_rb_gradient());
    // lp = centre → norm.x = 0.5 → t = 0.5
    // Interpolated sRGB: (0.5, 0, 0.5) → linear: (0.214, 0, 0.214)
    Color c = resolve_stroke_gradient_color(s, {100.0f, 100.0f}, {200.0f, 200.0f});
    // sRGB(0.5, 0, 0.5) → linear space
    const float half_lin = Color{0.5f, 0.0f, 0.5f, 1.0f}.to_linear().r;
    CHECK(c.r == doctest::Approx(half_lin).epsilon(0.01f));
    CHECK(c.g == doctest::Approx(0.0f).epsilon(1e-5f));
    CHECK(c.b == doctest::Approx(half_lin).epsilon(0.01f));
}

TEST_CASE("resolve_stroke_gradient_color: linear clamps outside [0,1]") {
    Shape s = make_shape(ShapeType::Rect, make_linear_rb_gradient());
    // lp far left → norm.x < 0 → clamped to t=0 → red
    Color c_left = resolve_stroke_gradient_color(s, {-50.0f, 100.0f}, {200.0f, 200.0f});
    Color expected_red = Color{1.0f, 0.0f, 0.0f, 1.0f}.to_linear();
    CHECK(c_left.r == doctest::Approx(expected_red.r).epsilon(1e-5f));

    // lp far right → norm.x > 1 → clamped to t=1 → blue
    Color c_right = resolve_stroke_gradient_color(s, {500.0f, 100.0f}, {200.0f, 200.0f});
    Color expected_blue = Color{0.0f, 0.0f, 1.0f, 1.0f}.to_linear();
    CHECK(c_right.b == doctest::Approx(expected_blue.b).epsilon(1e-5f));
}

TEST_CASE("resolve_stroke_gradient_color: linear on RoundedRect works") {
    Shape s = make_shape(ShapeType::RoundedRect, make_linear_rb_gradient());
    Color c = resolve_stroke_gradient_color(s, {0.0f, 100.0f}, {200.0f, 200.0f});
    Color expected = Color{1.0f, 0.0f, 0.0f, 1.0f}.to_linear();
    CHECK(c.r == doctest::Approx(expected.r).epsilon(1e-5f));
}

TEST_CASE("resolve_stroke_gradient_color: linear on Circle works") {
    Shape s = make_shape(ShapeType::Circle, make_linear_rb_gradient());
    Color c = resolve_stroke_gradient_color(s, {0.0f, 100.0f}, {200.0f, 200.0f});
    Color expected = Color{1.0f, 0.0f, 0.0f, 1.0f}.to_linear();
    CHECK(c.r == doctest::Approx(expected.r).epsilon(1e-5f));
}

// ══════════════════════════════════════════════════════════════════════════
// resolve_stroke_gradient_color — Radial gradient
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("resolve_stroke_gradient_color: radial at centre returns white") {
    Shape s = make_shape(ShapeType::Rect, make_radial_wb_gradient());
    Vec2 sz{200.0f, 200.0f};
    // At centre: norm=(0.5, 0.5), delta=(0,0), radius=0.5 → t=0 → white
    Color c = resolve_stroke_gradient_color(s, {100.0f, 100.0f}, sz);
    Color expected = Color{1.0f, 1.0f, 1.0f, 1.0f}.to_linear();
    CHECK(c.r == doctest::Approx(expected.r).epsilon(1e-5f));
    CHECK(c.g == doctest::Approx(expected.g).epsilon(1e-5f));
    CHECK(c.b == doctest::Approx(expected.b).epsilon(1e-5f));
}

TEST_CASE("resolve_stroke_gradient_color: radial at edge returns black") {
    Shape s = make_shape(ShapeType::Rect, make_radial_wb_gradient());
    Vec2 sz{200.0f, 200.0f};
    // At right edge: norm=(1.0, 0.5), delta=(0.5, 0), radius=0.5 → t=1 → black
    Color c = resolve_stroke_gradient_color(s, {200.0f, 100.0f}, sz);
    Color expected = Color{0.0f, 0.0f, 0.0f, 1.0f}.to_linear();
    CHECK(c.r == doctest::Approx(expected.r).epsilon(1e-5f));
    CHECK(c.g == doctest::Approx(expected.g).epsilon(1e-5f));
    CHECK(c.b == doctest::Approx(expected.b).epsilon(1e-5f));
}

// ══════════════════════════════════════════════════════════════════════════
// resolve_stroke_gradient_color — Edge cases
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("resolve_stroke_gradient_color: no gradient returns transparent") {
    Shape s = make_shape(ShapeType::Rect);
    Color c = resolve_stroke_gradient_color(s, {100.0f, 100.0f}, {200.0f, 200.0f});
    CHECK(c.r == doctest::Approx(0.0f));
    CHECK(c.g == doctest::Approx(0.0f));
    CHECK(c.b == doctest::Approx(0.0f));
    CHECK(c.a == doctest::Approx(0.0f));
}

TEST_CASE("resolve_stroke_gradient_color: None type returns transparent") {
    Shape s;
    s.type = ShapeType::None;
    Color c = resolve_stroke_gradient_color(s, {100.0f, 100.0f}, {200.0f, 200.0f});
    CHECK(c.r == doctest::Approx(0.0f));
    CHECK(c.g == doctest::Approx(0.0f));
    CHECK(c.b == doctest::Approx(0.0f));
    CHECK(c.a == doctest::Approx(0.0f));
}

TEST_CASE("resolve_stroke_gradient_color: Line type returns transparent") {
    Shape s;
    s.type = ShapeType::Line;
    Color c = resolve_stroke_gradient_color(s, {100.0f, 100.0f}, {200.0f, 200.0f});
    CHECK(c.r == doctest::Approx(0.0f));
    CHECK(c.g == doctest::Approx(0.0f));
    CHECK(c.b == doctest::Approx(0.0f));
    CHECK(c.a == doctest::Approx(0.0f));
}

TEST_CASE("resolve_stroke_gradient_color: Text type returns transparent") {
    Shape s;
    s.type = ShapeType::Text;
    Color c = resolve_stroke_gradient_color(s, {100.0f, 100.0f}, {200.0f, 200.0f});
    CHECK(c.r == doctest::Approx(0.0f));
    CHECK(c.g == doctest::Approx(0.0f));
    CHECK(c.b == doctest::Approx(0.0f));
    CHECK(c.a == doctest::Approx(0.0f));
}

TEST_CASE("resolve_stroke_gradient_color: gradient with single stop uses that color") {
    GradientFill g;
    g.type  = FillType::LinearGradient;
    g.from  = {0.0f, 0.5f};
    g.to    = {1.0f, 0.5f};
    g.stops = {{0.0f, {0.2f, 0.4f, 0.6f, 1.0f}}};

    Shape s = make_shape(ShapeType::Rect, g);
    Color c = resolve_stroke_gradient_color(s, {100.0f, 100.0f}, {200.0f, 200.0f});
    Color expected = Color{0.2f, 0.4f, 0.6f, 1.0f}.to_linear();
    CHECK(c.r == doctest::Approx(expected.r).epsilon(1e-5f));
    CHECK(c.g == doctest::Approx(expected.g).epsilon(1e-5f));
    CHECK(c.b == doctest::Approx(expected.b).epsilon(1e-5f));
}

TEST_CASE("resolve_stroke_gradient_color: zero-length gradient direction returns first stop") {
    // When from == to, the direction length is 0, and t stays 0.
    GradientFill g;
    g.type  = FillType::LinearGradient;
    g.from  = {0.5f, 0.5f};
    g.to    = {0.5f, 0.5f};  // zero-length
    g.stops = {
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
    };

    Shape s = make_shape(ShapeType::Rect, g);
    Color c = resolve_stroke_gradient_color(s, {100.0f, 100.0f}, {200.0f, 200.0f});
    Color expected = Color{1.0f, 0.0f, 0.0f, 1.0f}.to_linear();
    CHECK(c.r == doctest::Approx(expected.r).epsilon(1e-5f));
}

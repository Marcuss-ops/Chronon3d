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
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <rasterizers/shape_rasterizer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/graphics/gradient.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include "src/backends/software/rasterizers/shape_rasterizer_helpers.hpp"
#include <cmath>
using namespace chronon3d;

using namespace chronon3d::renderer;

// ── Helpers ─────────────────────────────────────────────────────────────

/// Build a simple red→blue linear gradient fill for a shape stroke.
static GradientFill make_linear_rb_gradient() {
    return graphics::GradientDefinition::linear(
        {0.0f, 0.5f}, {1.0f, 0.5f}, {
            {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
            {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
        });
}

/// Build a white→black radial gradient fill for a shape stroke.
static GradientFill make_radial_wb_gradient() {
    return graphics::GradientDefinition::radial(
        {0.5f, 0.5f}, 0.5f, {
            {0.0f, {1.0f, 1.0f, 1.0f, 1.0f}},
            {1.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
        });
}

/// Build a Shape with the given type and an optional gradient on the stroke.
static Shape make_shape(ShapeType type, const std::optional<GradientFill>& grad = std::nullopt,
                        f32 stroke_w = 10.0f) {
    Shape s;
    s.set_type(type);
    ShapeStroke stroke;
    stroke.enabled  = true;
    stroke.width    = stroke_w;
    stroke.color    = {0.5f, 0.5f, 0.5f, 1.0f};
    stroke.gradient = grad;
    switch (type) {
        case ShapeType::Rect:
            s.rect().size   = {200.0f, 200.0f};
            s.rect().stroke = stroke;
            break;
        case ShapeType::RoundedRect:
            s.rounded_rect().size   = {200.0f, 200.0f};
            s.rounded_rect().radius = 10.0f;
            s.rounded_rect().stroke = stroke;
            break;
        case ShapeType::Circle:
            s.circle().radius = 100.0f;
            s.circle().stroke = stroke;
            break;
        default:
            break;
    }
    return s;
}

// ── Dither-aware expectations ────────────────────────────────────────────

TEST_CASE("stroke alignment uses authored widths for center rect and inside circle") {
    Shape rect;
    rect.set_type(ShapeType::Rect);
    rect.rect().size = {20.0f, 20.0f};
    rect.rect().stroke.enabled = true;
    rect.rect().stroke.width = 4.0f;
    rect.rect().stroke.alignment = StrokeAlignment::Center;
    CHECK(hit_test_shape_stroke(rect, {-1.5f, 10.0f}, 0.0f, 0.0f));
    CHECK_FALSE(hit_test_shape_stroke(rect, {-2.5f, 10.0f}, 0.0f, 0.0f));

    Shape circle;
    circle.set_type(ShapeType::Circle);
    circle.circle().radius = 10.0f;
    circle.circle().stroke.enabled = true;
    circle.circle().stroke.width = 4.0f;
    circle.circle().stroke.alignment = StrokeAlignment::Inside;
    CHECK(hit_test_shape_stroke(circle, {17.0f, 10.0f}, 0.0f, 0.0f));
    CHECK_FALSE(hit_test_shape_stroke(circle, {15.0f, 10.0f}, 0.0f, 0.0f));
}

TEST_CASE("stroke raster pixels pin centered outer edge and full inside-circle width") {
    RenderState state;
    state.matrix = Mat4{1.0f};
    state.matrix[3][0] = 8.0f;
    state.matrix[3][1] = 8.0f;
    state.opacity = 1.0f;
    const auto transparent = Color::transparent().to_linear();

    Shape rect;
    rect.set_type(ShapeType::Rect);
    rect.rect().size = {16.0f, 16.0f};
    rect.rect().stroke.enabled = true;
    rect.rect().stroke.width = 4.0f;
    rect.rect().stroke.color = Color::white();
    rect.rect().stroke.alignment = StrokeAlignment::Center;
    Framebuffer rect_pixels(32, 32);
    draw_transformed_shape(rect_pixels, rect, state.matrix, transparent, 0.0f, &state);
    CHECK(rect_pixels.get_pixel(7, 12).a > 0.5f); // 0.5 px outside the rect
    CHECK(rect_pixels.get_pixel(5, 12).a == doctest::Approx(0.0f)); // 2.5 px outside

    Shape circle;
    circle.set_type(ShapeType::Circle);
    circle.circle().radius = 10.0f;
    circle.circle().stroke.enabled = true;
    circle.circle().stroke.width = 4.0f;
    circle.circle().stroke.color = Color::white();
    circle.circle().stroke.alignment = StrokeAlignment::Inside;
    Framebuffer circle_pixels(32, 32);
    draw_transformed_shape(circle_pixels, circle, state.matrix, transparent, 0.0f, &state);
    CHECK(circle_pixels.get_pixel(25, 18).a > 0.5f); // radius 7.5, inside the 4 px band
    CHECK(circle_pixels.get_pixel(23, 18).a == doctest::Approx(0.0f)); // radius 5.5, un-stroked interior
}
//
// resolve_stroke_gradient_color routes every sample through the renderer's
// spatial ordered dither (graphics::dither_gradient_sample): a deliberate
// ±0.5/255 per-channel adjustment keyed on the sampled pixel coordinate,
// introduced as a banding quality fix and pinned by
// tests/deterministic/gradient_determinism_tests.cpp.  Alpha is never
// dithered, and the adjustment is clamped to [0,1].
//
// The analytic stop colour is therefore only the undithered *limit* of this
// helper's contract.  No dither cell has a zero token (the table holds 0..63
// and the offset is (token + 0.5)/64 - 0.5), so exact equality with the
// analytic value can never hold.  Expectations compare with the renderer's
// own amplitude, which still rejects any real change of stop, interpolation
// or normalisation by a margin of ~500.
static constexpr f32 kDitherAmplitude = 0.5f / 255.0f;

/// True when `sampled` is `analytic` plus the renderer's bounded ordered dither.
[[nodiscard]] static bool is_gradient_channel(f32 sampled, f32 analytic) {
    return std::abs(sampled - analytic) <= kDitherAmplitude;
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
    s.set_type(ShapeType::None);
    CHECK_FALSE(stroke_has_gradient(s));
}

TEST_CASE("software rounded_rect fill selects the authored radius for each corner") {
    Shape shape;
    shape.set_type(ShapeType::RoundedRect);
    shape.rounded_rect().size = {100.0f, 100.0f};
    shape.rounded_rect().corner_radius = std::array<f32, 4>{30.0f, 20.0f, 10.0f, 0.0f};
    CHECK(hit_test_shape_fill(shape, {10.0f, 10.0f}, 0.0f, 0.0f));
    CHECK_FALSE(hit_test_shape_fill(shape, {0.0f, 0.0f}, 0.0f, 0.0f));
    CHECK_FALSE(hit_test_shape_fill(shape, {99.0f, 1.0f}, 0.0f, 0.0f));
    CHECK_FALSE(hit_test_shape_fill(shape, {99.0f, 99.0f}, 0.0f, 0.0f));
    CHECK(hit_test_shape_fill(shape, {0.0f, 99.0f}, 0.0f, 0.0f));
}

TEST_CASE("stroke_has_gradient: disabled stroke with gradient still returns true") {
    // stroke_has_gradient only checks gradient.has_value(), not enabled.
    auto grad = make_linear_rb_gradient();
    Shape s = make_shape(ShapeType::Rect, grad);
    s.rect().stroke.enabled = false;
    CHECK(stroke_has_gradient(s)); // gradient IS present even if disabled
}

TEST_CASE("stroke_has_gradient: Line type returns false") {
    Shape s;
    s.set_type(ShapeType::Line);
    CHECK_FALSE(stroke_has_gradient(s));
}

TEST_CASE("stroke_has_gradient: Text type returns false") {
    Shape s;
    s.set_type(ShapeType::None);
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
    CHECK(is_gradient_channel(c.r, expected.r));
    CHECK(is_gradient_channel(c.g, expected.g));
    CHECK(is_gradient_channel(c.b, expected.b));
}

TEST_CASE("resolve_stroke_gradient_color: linear at t=1 returns last stop (blue)") {
    Shape s = make_shape(ShapeType::Rect, make_linear_rb_gradient());
    // lp = right edge → norm.x = 1 → t = 1 → blue
    Color c = resolve_stroke_gradient_color(s, {200.0f, 100.0f}, {200.0f, 200.0f});
    Color expected = Color{0.0f, 0.0f, 1.0f, 1.0f}.to_linear();
    CHECK(is_gradient_channel(c.r, expected.r));
    CHECK(is_gradient_channel(c.g, expected.g));
    CHECK(is_gradient_channel(c.b, expected.b));
}

TEST_CASE("resolve_stroke_gradient_color: linear at t=0.5 interpolates") {
    Shape s = make_shape(ShapeType::Rect, make_linear_rb_gradient());
    // lp = centre → norm.x = 0.5 → t = 0.5
    // Authoring stops are sRGB; gradients are sampled in the renderer's
    // linear working space (canonical GradientDefinition contract — see
    // sample_color_stops in graphics/gradient.hpp).  Both stops convert to
    // linear (red 1,0,0 and blue 0,0,1 are already linear primaries), so
    // the midpoint is 0.5 linear, NOT linear(sRGB 0.5) ≈ 0.214.
    Color c = resolve_stroke_gradient_color(s, {100.0f, 100.0f}, {200.0f, 200.0f});
    CHECK(c.r == doctest::Approx(0.5f).epsilon(0.01f));
    CHECK(is_gradient_channel(c.g, 0.0f));
    CHECK(c.b == doctest::Approx(0.5f).epsilon(0.01f));
}

TEST_CASE("resolve_stroke_gradient_color: linear clamps outside [0,1]") {
    Shape s = make_shape(ShapeType::Rect, make_linear_rb_gradient());
    // lp far left → norm.x < 0 → clamped to t=0 → red
    Color c_left = resolve_stroke_gradient_color(s, {-50.0f, 100.0f}, {200.0f, 200.0f});
    Color expected_red = Color{1.0f, 0.0f, 0.0f, 1.0f}.to_linear();
    CHECK(is_gradient_channel(c_left.r, expected_red.r));

    // lp far right → norm.x > 1 → clamped to t=1 → blue
    Color c_right = resolve_stroke_gradient_color(s, {500.0f, 100.0f}, {200.0f, 200.0f});
    Color expected_blue = Color{0.0f, 0.0f, 1.0f, 1.0f}.to_linear();
    CHECK(is_gradient_channel(c_right.b, expected_blue.b));
}

TEST_CASE("resolve_stroke_gradient_color: linear on RoundedRect works") {
    Shape s = make_shape(ShapeType::RoundedRect, make_linear_rb_gradient());
    Color c = resolve_stroke_gradient_color(s, {0.0f, 100.0f}, {200.0f, 200.0f});
    Color expected = Color{1.0f, 0.0f, 0.0f, 1.0f}.to_linear();
    CHECK(is_gradient_channel(c.r, expected.r));
}

TEST_CASE("resolve_stroke_gradient_color: linear on Circle works") {
    Shape s = make_shape(ShapeType::Circle, make_linear_rb_gradient());
    Color c = resolve_stroke_gradient_color(s, {0.0f, 100.0f}, {200.0f, 200.0f});
    Color expected = Color{1.0f, 0.0f, 0.0f, 1.0f}.to_linear();
    CHECK(is_gradient_channel(c.r, expected.r));
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
    CHECK(is_gradient_channel(c.r, expected.r));
    CHECK(is_gradient_channel(c.g, expected.g));
    CHECK(is_gradient_channel(c.b, expected.b));
}

TEST_CASE("resolve_stroke_gradient_color: radial at edge returns black") {
    Shape s = make_shape(ShapeType::Rect, make_radial_wb_gradient());
    Vec2 sz{200.0f, 200.0f};
    // At right edge: norm=(1.0, 0.5), delta=(0.5, 0), radius=0.5 → t=1 → black
    Color c = resolve_stroke_gradient_color(s, {200.0f, 100.0f}, sz);
    Color expected = Color{0.0f, 0.0f, 0.0f, 1.0f}.to_linear();
    CHECK(is_gradient_channel(c.r, expected.r));
    CHECK(is_gradient_channel(c.g, expected.g));
    CHECK(is_gradient_channel(c.b, expected.b));
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
    s.set_type(ShapeType::None);
    Color c = resolve_stroke_gradient_color(s, {100.0f, 100.0f}, {200.0f, 200.0f});
    CHECK(c.r == doctest::Approx(0.0f));
    CHECK(c.g == doctest::Approx(0.0f));
    CHECK(c.b == doctest::Approx(0.0f));
    CHECK(c.a == doctest::Approx(0.0f));
}

TEST_CASE("resolve_stroke_gradient_color: Line type returns transparent") {
    Shape s;
    s.set_type(ShapeType::Line);
    Color c = resolve_stroke_gradient_color(s, {100.0f, 100.0f}, {200.0f, 200.0f});
    CHECK(c.r == doctest::Approx(0.0f));
    CHECK(c.g == doctest::Approx(0.0f));
    CHECK(c.b == doctest::Approx(0.0f));
    CHECK(c.a == doctest::Approx(0.0f));
}

TEST_CASE("resolve_stroke_gradient_color: Text type returns transparent") {
    Shape s;
    s.set_type(ShapeType::None);
    Color c = resolve_stroke_gradient_color(s, {100.0f, 100.0f}, {200.0f, 200.0f});
    CHECK(c.r == doctest::Approx(0.0f));
    CHECK(c.g == doctest::Approx(0.0f));
    CHECK(c.b == doctest::Approx(0.0f));
    CHECK(c.a == doctest::Approx(0.0f));
}

TEST_CASE("resolve_stroke_gradient_color: gradient with single stop uses that color") {
    GradientFill g = graphics::GradientDefinition::linear(
        {0.0f, 0.5f}, {1.0f, 0.5f}, {{0.0f, {0.2f, 0.4f, 0.6f, 1.0f}}});

    Shape s = make_shape(ShapeType::Rect, g);
    Color c = resolve_stroke_gradient_color(s, {100.0f, 100.0f}, {200.0f, 200.0f});
    Color expected = Color{0.2f, 0.4f, 0.6f, 1.0f}.to_linear();
    CHECK(is_gradient_channel(c.r, expected.r));
    CHECK(is_gradient_channel(c.g, expected.g));
    CHECK(is_gradient_channel(c.b, expected.b));
}

TEST_CASE("canonical gradient keeps conic geometry, opacity stops, and spread while sampling") {
    auto gradient = graphics::GradientDefinition::conic(
        {0.5f, 0.5f}, 0.0f,
        {{0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
         {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}}},
        graphics::GradientSpread::Reflect);
    gradient.opacity_stops = {{0.0f, 0.25f}, {1.0f, 0.75f}};

    Shape shape = make_shape(ShapeType::Rect, gradient);
    REQUIRE(shape.rect().stroke.gradient.has_value());
    const auto& stored = *shape.rect().stroke.gradient;
    CHECK(stored.type == graphics::GradientType::Conic);
    CHECK(stored.center.x == doctest::Approx(0.5f));
    CHECK(stored.angle == doctest::Approx(0.0f));
    CHECK(stored.spread == graphics::GradientSpread::Reflect);
    REQUIRE(stored.opacity_stops.size() == 2);

    const Color sampled = resolve_stroke_gradient_color(
        shape, {100.0f, 150.0f}, {200.0f, 200.0f});

    // r/b carry the dither; alpha is the untouched opacity-stop product.
    CHECK(is_gradient_channel(sampled.r, 0.75f));
    CHECK(is_gradient_channel(sampled.b, 0.25f));
    CHECK(sampled.a == doctest::Approx(0.375f));
}

TEST_CASE("canonical gradient hash includes spread, geometry, and opacity stops") {
    Fill first = Fill::linear(
        {0.0f, 0.0f}, {1.0f, 0.0f},
        {{0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
         {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}}});
    Fill changed = first;
    changed.gradient.spread = graphics::GradientSpread::Repeat;
    CHECK(graph::hash_fill(first) != graph::hash_fill(changed));
    changed = first;
    changed.gradient.opacity_stops = {{0.0f, 0.25f}, {1.0f, 1.0f}};
    CHECK(graph::hash_fill(first) != graph::hash_fill(changed));
}

TEST_CASE("resolve_stroke_gradient_color: zero-length gradient direction returns first stop") {
    // When from == to, the direction length is 0, and t stays 0.
    GradientFill g = graphics::GradientDefinition::linear(
        {0.5f, 0.5f}, {0.5f, 0.5f}, {
            {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
            {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
        });

    Shape s = make_shape(ShapeType::Rect, g);
    Color c = resolve_stroke_gradient_color(s, {100.0f, 100.0f}, {200.0f, 200.0f});
    Color expected = Color{1.0f, 0.0f, 0.0f, 1.0f}.to_linear();
    CHECK(is_gradient_channel(c.r, expected.r));
    CHECK(is_gradient_channel(c.g, 0.0f));
    CHECK(is_gradient_channel(c.b, 0.0f));
}

// ══════════════════════════════════════════════════════════════════════════
// resolve_stroke_gradient_color — ordered dither
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("resolve_stroke_gradient_color: dithers the sampled stop per pixel") {
    // The resolver samples through the renderer's ordered dither, so two
    // pixels sharing one analytic colour must differ by exactly the
    // canonical dither token of their cells — spatial, bounded, and keyed
    // on the sampled coordinate rather than on the authored colour.  A
    // single-stop gradient keeps the analytic colour constant so the dither
    // is the only variable between the two samples.
    GradientFill g = graphics::GradientDefinition::linear(
        {0.0f, 0.5f}, {1.0f, 0.5f}, {{0.0f, {0.2f, 0.4f, 0.6f, 1.0f}}});
    Shape s = make_shape(ShapeType::Rect, g);
    const Vec2 sz{200.0f, 200.0f};
    const Color analytic = Color{0.2f, 0.4f, 0.6f, 1.0f}.to_linear();

    // Cells (0, 100) and (1, 100) hold different Bayer tokens (3 and 35).
    const Color first = resolve_stroke_gradient_color(s, {0.0f, 100.0f}, sz);
    const Color next  = resolve_stroke_gradient_color(s, {1.0f, 100.0f}, sz);

    CHECK(first.r == doctest::Approx(
        graphics::dither_gradient_sample(analytic, 0.0f, 100.0f).r).epsilon(1e-5f));
    CHECK(next.r == doctest::Approx(
        graphics::dither_gradient_sample(analytic, 1.0f, 100.0f).r).epsilon(1e-5f));
    CHECK(first.r != next.r);                            // spatial, not constant
    CHECK(is_gradient_channel(first.r, analytic.r));     // still bounded
    CHECK(first.a == doctest::Approx(analytic.a));       // alpha is never dithered
}

// ══════════════════════════════════════════════════════════════════════════
// safe_shape_size_for_fill — zero-size guard
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("safe_shape_size_for_fill: unknown shape returns {1,1}") {
    Shape s;
    s.set_type(ShapeType::None);
    Vec2 sz = safe_shape_size_for_fill(s);
    CHECK(sz.x > 0.0f);
    CHECK(sz.y > 0.0f);
    CHECK(sz.x == doctest::Approx(1.0f));
    CHECK(sz.y == doctest::Approx(1.0f));
}

TEST_CASE("safe_shape_size_for_fill: Rect with positive size returns it") {
    Shape s;
    s.set_type(ShapeType::Rect);
    s.rect().size = {200.0f, 100.0f};
    Vec2 sz = safe_shape_size_for_fill(s);
    CHECK(sz.x == doctest::Approx(200.0f));
    CHECK(sz.y == doctest::Approx(100.0f));
}

TEST_CASE("safe_shape_size_for_fill: None type returns {1,1}") {
    Shape s;
    s.set_type(ShapeType::None);
    Vec2 sz = safe_shape_size_for_fill(s);
    CHECK(sz.x > 0.0f);
    CHECK(sz.y > 0.0f);
    CHECK(sz.x == doctest::Approx(1.0f));
    CHECK(sz.y == doctest::Approx(1.0f));
}

TEST_CASE("safe_shape_size_for_fill: Circle with positive radius returns diameter") {
    Shape s;
    s.set_type(ShapeType::Circle);
    s.circle().radius = 50.0f;
    Vec2 sz = safe_shape_size_for_fill(s);
    CHECK(sz.x == doctest::Approx(100.0f));
    CHECK(sz.y == doctest::Approx(100.0f));
}

// ══════════════════════════════════════════════════════════════════════════
// resolve_gradient_color — division-by-zero guard (sz={0,0})
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("resolve_gradient_color: zero-size sz does not produce NaN/Inf") {
    GradientFill g = graphics::GradientDefinition::linear(
        {0.0f, 0.5f}, {1.0f, 0.5f}, {
            {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
            {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
        });
    Fill fill;
    fill.type = FillType::LinearGradient;
    fill.gradient = g;

    // Pass sz={0,0} — the guard should clamp to sx=1, sy=1 and produce valid colour.
    Color c = resolve_gradient_color(fill, {100.0f, 50.0f}, {0.0f, 0.0f}, 1.0f);
    CHECK_FALSE(std::isnan(c.r));
    CHECK_FALSE(std::isnan(c.g));
    CHECK_FALSE(std::isnan(c.b));
    CHECK_FALSE(std::isnan(c.a));
    CHECK_FALSE(std::isinf(c.r));
    CHECK_FALSE(std::isinf(c.g));
    CHECK_FALSE(std::isinf(c.b));
    CHECK_FALSE(std::isinf(c.a));
}

TEST_CASE("resolve_gradient_color: zero-size sz with radial gradient does not crash") {
    GradientFill g = graphics::GradientDefinition::radial(
        {0.5f, 0.5f}, 0.5f, {
            {0.0f, {1.0f, 1.0f, 1.0f, 1.0f}},
            {1.0f, {0.0f, 0.0f, 0.0f, 1.0f}},
        });
    Fill fill;
    fill.type = FillType::RadialGradient;
    fill.gradient = g;

    Color c = resolve_gradient_color(fill, {50.0f, 50.0f}, {0.0f, 0.0f}, 1.0f);
    CHECK_FALSE(std::isnan(c.r));
    CHECK_FALSE(std::isnan(c.g));
    CHECK_FALSE(std::isnan(c.b));
    CHECK_FALSE(std::isnan(c.a));
}

// ── FillStyle integration tests ──────────────────────────────────────
//
// Renders compositions with gradient fills driven by KeyframeTrack and
// verifies pixel-level output across frames.
//
// Uses s.rect(...) directly (matching golden test pattern in
// tests/golden/suite_chronon3d_tests.cpp).
//
// NOTE: The framebuffer stores linear-space colours.  Assertion thresholds
// must account for sRGB→linear conversion (e.g. sRGB 0.5 → linear ≈ 0.214).

#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <tests/helpers/sample_helpers.hpp>
using namespace chronon3d;

namespace gfx = chronon3d::graphics;
using chronon3d::test::sample_center;
using chronon3d::test::sample_left;
using chronon3d::test::sample_right;

static std::shared_ptr<Framebuffer> render_frame(const Composition& comp, Frame frame) {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    return renderer.render_frame(comp, frame);
}

// ===========================================================================
// Static gradient rendering
// ===========================================================================

TEST_CASE("FillStyle linear gradient renders left red right blue") {
    constexpr int W = 200, H = 200;

    auto stops = std::vector<gfx::GradientStop>{
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    const auto fill = gfx::FillStyle::linear({0.0f, 0.5f}, {1.0f, 0.5f}, stops);

    Composition comp(CompositionSpec{.name = "GradRect", .width = W, .height = H, .duration = 1},
        [&](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.rect("grad", {.size = {W*1.0f, H*1.0f}, .color = Color::white(), .pos = {0,0,0}, .fill = fill});
            return s.build();
        });

    auto fb = render_frame(comp, Frame{0});
    REQUIRE(fb != nullptr);

    Color left = sample_left(*fb, 15);
    CHECK(left.r > 0.7f);
    CHECK(left.b < 0.4f);

    Color right = sample_right(*fb, 15);
    CHECK(right.b > 0.7f);
    CHECK(right.r < 0.4f);

    Color mid = sample_center(*fb, 8);
    CHECK(mid.r == doctest::Approx(0.5f).epsilon(0.15f));
    CHECK(mid.b == doctest::Approx(0.5f).epsilon(0.15f));
}

// ===========================================================================
// Animated gradient via KeyframeTrack
// ===========================================================================

TEST_CASE("KeyframeTrack<FillStyle> animates gradient red-blue to green-yellow") {
    constexpr int W = 200, H = 200;

    auto stops_a = std::vector<gfx::GradientStop>{
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    const auto grad_a = gfx::FillStyle::linear({0.0f, 0.5f}, {1.0f, 0.5f}, stops_a);

    auto stops_b = std::vector<gfx::GradientStop>{
        {0.0f, {0.0f, 1.0f, 0.0f, 1.0f}},
        {1.0f, {1.0f, 1.0f, 0.0f, 1.0f}},
    };
    const auto grad_b = gfx::FillStyle::linear({0.0f, 0.5f}, {1.0f, 0.5f}, stops_b);

    KeyframeTrack<gfx::FillStyle> fill_track;
    fill_track.key(Frame{0},  grad_a)
              .key(Frame{60}, grad_b);

    // Frame 0: red->blue gradient.
    {
        Composition comp(CompositionSpec{.name = "AF0", .width = W, .height = H, .duration = 1},
            [&](const FrameContext& ctx) {
                SceneBuilder s(ctx);
                s.rect("grad", {.size = {W*1.0f, H*1.0f}, .color = Color::white(), .pos = {0,0,0}, .fill = fill_track.sample_at(0.0f)});
                return s.build();
            });
        auto fb = render_frame(comp, Frame{0});
        REQUIRE(fb != nullptr);
        CHECK(sample_left(*fb, 15).r > 0.7f);
        CHECK(sample_right(*fb, 15).b > 0.7f);
    }

    // Frame 60: green->yellow gradient.
    {
        Composition comp(CompositionSpec{.name = "AF60", .width = W, .height = H, .duration = 1},
            [&](const FrameContext& ctx) {
                SceneBuilder s(ctx);
                s.rect("grad", {.size = {W*1.0f, H*1.0f}, .color = Color::white(), .pos = {0,0,0}, .fill = fill_track.sample_at(60.0f)});
                return s.build();
            });
        auto fb = render_frame(comp, Frame{60});
        REQUIRE(fb != nullptr);
        Color l60 = sample_left(*fb, 15);
        CHECK(l60.g > 0.7f);
        Color r60 = sample_right(*fb, 15);
        CHECK(r60.r > 0.5f);
        CHECK(r60.g > 0.5f);
    }

    // Frame 30: interpolated — colors are at ~sRGB 0.5 which is linear ~0.214.
    {
        Composition comp(CompositionSpec{.name = "AF30", .width = W, .height = H, .duration = 1},
            [&](const FrameContext& ctx) {
                SceneBuilder s(ctx);
                s.rect("grad", {.size = {W*1.0f, H*1.0f}, .color = Color::white(), .pos = {0,0,0}, .fill = fill_track.sample_at(30.0f)});
                return s.build();
            });
        auto fb = render_frame(comp, Frame{30});
        REQUIRE(fb != nullptr);
        Color l30 = sample_left(*fb, 15);
        Color r30 = sample_right(*fb, 15);
        // Linear-space values are ~0.214 (sRGB 0.5 → linear).
        CHECK(l30.r > 0.15f);
        CHECK(l30.g > 0.15f);
        CHECK(r30.r > 0.15f);
        CHECK(r30.g > 0.15f);
        CHECK(r30.b > 0.15f);
    }
}

// ===========================================================================
// Solid -> gradient morph
// ===========================================================================

TEST_CASE("KeyframeTrack<FillStyle> morphs solid white to red-blue gradient") {
    constexpr int W = 200, H = 200;

    const auto solid_white = gfx::FillStyle::solid(Color{1.0f, 1.0f, 1.0f, 1.0f});

    auto stops = std::vector<gfx::GradientStop>{
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    const auto grad_rb = gfx::FillStyle::linear({0.0f, 0.5f}, {1.0f, 0.5f}, stops);

    KeyframeTrack<gfx::FillStyle> fill_track;
    fill_track.key(Frame{0},  solid_white)
              .key(Frame{60}, grad_rb);

    // Frame 0: solid white.
    {
        Composition comp(CompositionSpec{.name = "SM0", .width = W, .height = H, .duration = 1},
            [&](const FrameContext& ctx) {
                SceneBuilder s(ctx);
                s.rect("shape", {.size = {W*1.0f, H*1.0f}, .color = Color::white(), .pos = {0,0,0}, .fill = fill_track.sample_at(0.0f)});
                return s.build();
            });
        auto fb = render_frame(comp, Frame{0});
        REQUIRE(fb != nullptr);
        Color c0 = sample_center(*fb, 8);
        CHECK(c0.r > 0.9f);
        CHECK(c0.g > 0.9f);
        CHECK(c0.b > 0.9f);
    }

    // Frame 60: red -> blue gradient.
    {
        Composition comp(CompositionSpec{.name = "SM60", .width = W, .height = H, .duration = 1},
            [&](const FrameContext& ctx) {
                SceneBuilder s(ctx);
                s.rect("shape", {.size = {W*1.0f, H*1.0f}, .color = Color::white(), .pos = {0,0,0}, .fill = fill_track.sample_at(60.0f)});
                return s.build();
            });
        auto fb = render_frame(comp, Frame{60});
        REQUIRE(fb != nullptr);
        CHECK(sample_left(*fb, 15).r > 0.7f);
        CHECK(sample_right(*fb, 15).b > 0.7f);
    }

    // Frame 30: interpolated — left stays red-dominant, right has visible color.
    {
        Composition comp(CompositionSpec{.name = "SM30", .width = W, .height = H, .duration = 1},
            [&](const FrameContext& ctx) {
                SceneBuilder s(ctx);
                s.rect("shape", {.size = {W*1.0f, H*1.0f}, .color = Color::white(), .pos = {0,0,0}, .fill = fill_track.sample_at(30.0f)});
                return s.build();
            });
        auto fb = render_frame(comp, Frame{30});
        REQUIRE(fb != nullptr);
        Color l30 = sample_left(*fb, 15);
        CHECK(l30.r > 0.7f);
        Color r30 = sample_right(*fb, 15);
        // Linear value of sRGB(0.5, 0.5, 1) at t=0.925 → ~0.273 red.
        CHECK(r30.r > 0.2f);
    }
}

// ===========================================================================
// Radial gradient rendering
// ===========================================================================

TEST_CASE("Radial gradient fills centre white, edge blue") {
    constexpr int W = 200, H = 200;

    auto stops = std::vector<gfx::GradientStop>{
        {0.0f, {1.0f, 1.0f, 1.0f, 1.0f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    const auto fill = gfx::FillStyle::radial({0.5f, 0.5f}, 0.5f, stops);

    Composition comp(CompositionSpec{.name = "RadialGrad", .width = W, .height = H, .duration = 1},
        [&](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.rect("shape", {.size = {W*1.0f, H*1.0f}, .color = Color::white(), .pos = {0,0,0}, .fill = fill});
            return s.build();
        });

    auto fb = render_frame(comp, Frame{0});
    REQUIRE(fb != nullptr);

    // Centre should be white.
    Color c = sample_center(*fb, 4);
    CHECK(c.r > 0.9f);
    CHECK(c.g > 0.9f);
    CHECK(c.b > 0.9f);

    // Right edge should be blue dominant.
    Color r = sample_right(*fb, 5);
    CHECK(r.b > 0.5f);
}

// ===========================================================================
// Stroke gradient rendering
// ===========================================================================
//
// The rect is smaller than the framebuffer so the outer stroke half
// (outside the rect) falls within the framebuffer and fill_hit is false
// there (fill doesn't shadow the stroke).
//
// Color must have alpha > 0 to avoid the early return in
// draw_transformed_shape (shape_rasterizer.cpp line ~182).

TEST_CASE("StrokeStyle linear gradient renders right stroke gradient") {
    constexpr int W = 200, H = 200;
    constexpr f32 RECT_W = 120.0f, STROKE_W = 20.0f; // half = 10

    // Red->blue horizontal gradient stroke.
    auto stops = std::vector<gfx::GradientStop>{
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    const auto stroke = gfx::StrokeStyle::linear_gradient(
        {0.0f, 0.5f}, {1.0f, 0.5f}, stops, STROKE_W);

    Composition comp(CompositionSpec{.name = "StrokeGrad", .width = W, .height = H, .duration = 1},
        [&](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            // Use opaque color so the rasterizer doesn't early-return.
            // The rect is SMALLER than the framebuffer so the outer
            // half of the Center-aligned stroke falls inside the FB.
            s.rect("shape", {.size = {RECT_W, RECT_W}, .color = Color::white(), .pos = {0,0,0},
                .stroke = stroke});
            return s.build();
        });

    auto fb = render_frame(comp, Frame{0});
    REQUIRE(fb != nullptr);

    // Right outer stroke: local x ∈ [120, 130] (half=10 from rect edge).
    // Sample at local (125, 60) → screen (165, 100).
    // Gradient geometry from {0, 0.5} to {1, 0.5}: at norm_x ≈ 125/120 = 1.04 → t=1 → blue.
    Color right_stroke = fb->get_pixel(165, 100);
    CHECK(right_stroke.b > 0.7f);
    CHECK(right_stroke.r < 0.4f);

    // Inside the rect (fill area) should be opaque white.
    Color interior = fb->get_pixel(60, 100);
    CHECK(interior.r > 0.9f);
    CHECK(interior.g > 0.9f);
    CHECK(interior.b > 0.9f);
}

// ===========================================================================
// Animated stroke gradient via KeyframeTrack
// ===========================================================================

TEST_CASE("KeyframeTrack<StrokeStyle> animates gradient red-blue to green-yellow stroke") {
    constexpr int W = 200, H = 200;
    constexpr f32 RECT_W = 120.0f, STROKE_W = 20.0f; // half = 10

    auto stops_a = std::vector<gfx::GradientStop>{
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    const auto grad_a = gfx::StrokeStyle::linear_gradient(
        {0.0f, 0.5f}, {1.0f, 0.5f}, stops_a, STROKE_W);

    auto stops_b = std::vector<gfx::GradientStop>{
        {0.0f, {0.0f, 1.0f, 0.0f, 1.0f}},
        {1.0f, {1.0f, 1.0f, 0.0f, 1.0f}},
    };
    const auto grad_b = gfx::StrokeStyle::linear_gradient(
        {0.0f, 0.5f}, {1.0f, 0.5f}, stops_b, STROKE_W);

    KeyframeTrack<gfx::StrokeStyle> stroke_track;
    stroke_track.key(Frame{0},  grad_a)
                .key(Frame{60}, grad_b);

    // Frame 0: red->blue stroke → right edge is blue.
    {
        Composition comp(CompositionSpec{.name = "SA0", .width = W, .height = H, .duration = 1},
            [&](const FrameContext& ctx) {
                SceneBuilder s(ctx);
                s.rect("shape", {.size = {RECT_W, RECT_W}, .color = Color::white(), .pos = {0,0,0},
                    .stroke = stroke_track.sample_at(0.0f)});
                return s.build();
            });
        auto fb = render_frame(comp, Frame{0});
        REQUIRE(fb != nullptr);
        Color rs = fb->get_pixel(165, 100);
        CHECK(rs.b > 0.7f);
    }

    // Frame 60: green->yellow stroke → right edge is green+yellow.
    {
        Composition comp(CompositionSpec{.name = "SA60", .width = W, .height = H, .duration = 1},
            [&](const FrameContext& ctx) {
                SceneBuilder s(ctx);
                s.rect("shape", {.size = {RECT_W, RECT_W}, .color = Color::white(), .pos = {0,0,0},
                    .stroke = stroke_track.sample_at(60.0f)});
                return s.build();
            });
        auto fb = render_frame(comp, Frame{60});
        REQUIRE(fb != nullptr);
        Color rs = fb->get_pixel(165, 100);
        CHECK(rs.g > 0.5f);
    }

    // Frame 30: interpolated.
    {
        Composition comp(CompositionSpec{.name = "SA30", .width = W, .height = H, .duration = 1},
            [&](const FrameContext& ctx) {
                SceneBuilder s(ctx);
                s.rect("shape", {.size = {RECT_W, RECT_W}, .color = Color::white(), .pos = {0,0,0},
                    .stroke = stroke_track.sample_at(30.0f)});
                return s.build();
            });
        auto fb = render_frame(comp, Frame{30});
        REQUIRE(fb != nullptr);
        Color rs = fb->get_pixel(165, 100);
        // Interpolated: sRGB(0.5, 0.5, 0.5) → linear ≈ 0.218.
        CHECK(rs.r > 0.15f);
        CHECK(rs.g > 0.15f);
        CHECK(rs.b > 0.15f);
    }
}

// ===========================================================================
// Solid -> gradient stroke morph
// ===========================================================================

TEST_CASE("KeyframeTrack<StrokeStyle> morphs solid white stroke to red-blue gradient") {
    constexpr int W = 200, H = 200;
    constexpr f32 RECT_W = 120.0f, STROKE_W = 20.0f; // half = 10

    const auto solid_white = gfx::StrokeStyle::solid(Color{1.0f, 1.0f, 1.0f, 1.0f}, STROKE_W);

    auto stops = std::vector<gfx::GradientStop>{
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    const auto grad_rb = gfx::StrokeStyle::linear_gradient(
        {0.0f, 0.5f}, {1.0f, 0.5f}, stops, STROKE_W);

    KeyframeTrack<gfx::StrokeStyle> stroke_track;
    stroke_track.key(Frame{0},  solid_white)
                .key(Frame{60}, grad_rb);

    // Frame 0: solid white stroke → right edge is white.
    {
        Composition comp(CompositionSpec{.name = "SS0", .width = W, .height = H, .duration = 1},
            [&](const FrameContext& ctx) {
                SceneBuilder s(ctx);
                s.rect("shape", {.size = {RECT_W, RECT_W}, .color = Color::white(), .pos = {0,0,0},
                    .stroke = stroke_track.sample_at(0.0f)});
                return s.build();
            });
        auto fb = render_frame(comp, Frame{0});
        REQUIRE(fb != nullptr);
        Color c = fb->get_pixel(165, 100);
        CHECK(c.r > 0.9f);
        CHECK(c.g > 0.9f);
        CHECK(c.b > 0.9f);
    }

    // Frame 60: red->blue gradient stroke → right edge is blue.
    {
        Composition comp(CompositionSpec{.name = "SS60", .width = W, .height = H, .duration = 1},
            [&](const FrameContext& ctx) {
                SceneBuilder s(ctx);
                s.rect("shape", {.size = {RECT_W, RECT_W}, .color = Color::white(), .pos = {0,0,0},
                    .stroke = stroke_track.sample_at(60.0f)});
                return s.build();
            });
        auto fb = render_frame(comp, Frame{60});
        REQUIRE(fb != nullptr);
        CHECK(fb->get_pixel(165, 100).b > 0.7f);
    }

    // Frame 30: interpolated → right edge has visible color.
    {
        Composition comp(CompositionSpec{.name = "SS30", .width = W, .height = H, .duration = 1},
            [&](const FrameContext& ctx) {
                SceneBuilder s(ctx);
                s.rect("shape", {.size = {RECT_W, RECT_W}, .color = Color::white(), .pos = {0,0,0},
                    .stroke = stroke_track.sample_at(30.0f)});
                return s.build();
            });
        auto fb = render_frame(comp, Frame{30});
        REQUIRE(fb != nullptr);
        Color rs = fb->get_pixel(165, 100);
        // Interpolated: lerp(white, blue, 0.5) at right edge → linear ≈ 0.5.
        CHECK(rs.b > 0.4f);
    }
}

// ===========================================================================
// Determinism test
// ===========================================================================

TEST_CASE("Fill renders produce deterministic pixel output") {
    constexpr int W = 100, H = 100;

    auto stops = std::vector<gfx::GradientStop>{
        {0.0f, {0.8f, 0.2f, 0.3f, 1.0f}},
        {1.0f, {0.2f, 0.7f, 0.9f, 1.0f}},
    };
    const auto fill = gfx::FillStyle::linear({0.0f, 0.5f}, {1.0f, 0.5f}, stops);

    Composition comp(CompositionSpec{.name = "DetFill", .width = W, .height = H, .duration = 1},
        [&](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.rect("shape", {.size = {W*1.0f, H*1.0f}, .color = Color::white(), .pos = {0,0,0}, .fill = fill});
            return s.build();
        });

    auto fb_a = render_frame(comp, Frame{30});
    auto fb_b = render_frame(comp, Frame{30});
    REQUIRE(fb_a != nullptr);
    REQUIRE(fb_b != nullptr);

    bool identical = true;
    for (int y = 0; y < H && identical; ++y)
        for (int x = 0; x < W && identical; ++x) {
            Color ca = fb_a->get_pixel(x, y);
            Color cb = fb_b->get_pixel(x, y);
            if (std::abs(ca.r - cb.r) > 1.0f/255.0f ||
                std::abs(ca.g - cb.g) > 1.0f/255.0f ||
                std::abs(ca.b - cb.b) > 1.0f/255.0f ||
                std::abs(ca.a - cb.a) > 1.0f/255.0f)
                identical = false;
        }
    CHECK(identical);
}

#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

using namespace chronon3d;

static std::unique_ptr<Framebuffer> render_single(
    i32 w, i32 h,
    std::function<void(SceneBuilder&)> build_fn)
{
    SoftwareRenderer renderer;
    CompositionSpec spec{.width = w, .height = h, .duration = 1};
    Composition comp{spec, [&](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        build_fn(s);
        return s.build();
    }};
    return renderer.render_frame(comp, 0);
}

// ---------------------------------------------------------------------------
// RoundedRect
// ---------------------------------------------------------------------------
TEST_CASE("RoundedRect primitive") {
    // 100x100 canvas, rounded rect centered at (50,50) size 60x40 radius 10
    auto fb = render_single(100, 100, [](SceneBuilder& s) {
        s.rounded_rect("rr", {50, 50, 0}, {60, 40}, 10.0f, Color::white());
    });

    SUBCASE("Center pixel is filled") {
        CHECK(fb->get_pixel(50, 50).r == 1.0f);
    }

    SUBCASE("Inside body (not corner) is filled") {
        // Point at (50, 40) — top edge center, inside body
        CHECK(fb->get_pixel(50, 31).r == 1.0f);
    }

    SUBCASE("Corner outside arc is black") {
        // Corner of the bounding box (20,30) is outside the rounded corner
        CHECK(fb->get_pixel(21, 31).r == 0.0f);
        CHECK(fb->get_pixel(79, 31).r == 0.0f);
    }

    SUBCASE("Pixel just outside the rect is black") {
        CHECK(fb->get_pixel(50, 15).r == 0.0f);  // above
        CHECK(fb->get_pixel(50, 85).r == 0.0f);  // below
    }

    SUBCASE("Radius clamped — large radius still renders") {
        // radius > min(hw,hh) should be clamped, not crash
        auto fb2 = render_single(100, 100, [](SceneBuilder& s) {
            s.rounded_rect("rr", {50, 50, 0}, {60, 40}, 999.0f, Color::white());
        });
        CHECK(fb2->get_pixel(50, 50).r == 1.0f);
    }
}

// ---------------------------------------------------------------------------
// DropShadow
// ---------------------------------------------------------------------------
TEST_CASE("DropShadow") {
    SUBCASE("Shadow offset pixel is dark") {
        // White rounded rect at center, shadow offset 20px down
        auto fb = render_single(200, 200, [](SceneBuilder& s) {
            s.rounded_rect("card", {100, 80, 0}, {80, 40}, 8.0f, Color::white())
             .with_shadow(DropShadow{
                .enabled = true,
                .offset  = {0.0f, 20.0f},
                .color   = {0.0f, 0.0f, 0.0f, 0.8f},
                .radius  = 0.0f  // no blur for deterministic test
             });
        });
        // Shadow core is drawn at offset y=100+20=120, check a pixel there
        // Shadow is a rounded rect expanded by 0 at offset (100,100)
        CHECK(fb->get_pixel(100, 100).r < 0.5f);  // dark from shadow
        // Main card pixel is white
        CHECK(fb->get_pixel(100, 80).r > 0.9f);
    }

    SUBCASE("Shadow with alpha=0 leaves image unchanged") {
        auto fb_no_shadow = render_single(100, 100, [](SceneBuilder& s) {
            s.rounded_rect("card", {50, 50, 0}, {60, 40}, 8.0f, Color::white());
        });
        auto fb_shadow = render_single(100, 100, [](SceneBuilder& s) {
            s.rounded_rect("card", {50, 50, 0}, {60, 40}, 8.0f, Color::white())
             .with_shadow(DropShadow{
                .enabled = true,
                .offset  = {10.0f, 10.0f},
                .color   = {0.0f, 0.0f, 0.0f, 0.0f},  // alpha=0
                .radius  = 8.0f
             });
        });
        // Both framebuffers should be identical
        for (i32 y = 0; y < 100; ++y) {
            for (i32 x = 0; x < 100; ++x) {
                CHECK(fb_no_shadow->get_pixel(x, y).r == doctest::Approx(fb_shadow->get_pixel(x, y).r));
            }
        }
    }

    SUBCASE("Shape is on top of shadow") {
        // White card with black shadow at same position (offset 0)
        auto fb = render_single(100, 100, [](SceneBuilder& s) {
            s.rounded_rect("card", {50, 50, 0}, {60, 40}, 8.0f, Color::white())
             .with_shadow(DropShadow{
                .enabled = true,
                .offset  = {0.0f, 0.0f},
                .color   = {0.0f, 0.0f, 0.0f, 1.0f},
                .radius  = 0.0f
             });
        });
        // Center of the card should be white (shape paints over shadow)
        CHECK(fb->get_pixel(50, 50).r > 0.9f);
    }
}

// ---------------------------------------------------------------------------
// Glow
// ---------------------------------------------------------------------------
TEST_CASE("Glow") {
    SUBCASE("Glow extends pixels beyond circle radius") {
        // Blue circle radius 20, glow radius 15, at center of 100x100
        auto fb = render_single(100, 100, [](SceneBuilder& s) {
            s.circle("c", {50, 50, 0}, 20.0f, Color::blue())
             .with_glow(Glow{
                .enabled   = true,
                .radius    = 15.0f,
                .intensity = 1.0f,
                .color     = {0.0f, 0.0f, 1.0f, 1.0f}
             });
        });
        // Pixel at distance ~25 (outside circle r=20 but inside glow r=35) should have color
        CHECK(fb->get_pixel(75, 50).b > 0.0f);  // 25px from center, within glow
        // Pixel well outside glow should be black
        CHECK(fb->get_pixel(99, 50).b == 0.0f);
    }

    SUBCASE("Glow with intensity=0 produces no extra pixels") {
        auto fb_no_glow = render_single(100, 100, [](SceneBuilder& s) {
            s.circle("c", {50, 50, 0}, 20.0f, Color::blue());
        });
        auto fb_glow = render_single(100, 100, [](SceneBuilder& s) {
            s.circle("c", {50, 50, 0}, 20.0f, Color::blue())
             .with_glow(Glow{
                .enabled   = true,
                .radius    = 15.0f,
                .intensity = 0.0f,
                .color     = {0.0f, 0.0f, 1.0f, 1.0f}
             });
        });
        CHECK(fb_no_glow->get_pixel(75, 50).b == doctest::Approx(fb_glow->get_pixel(75, 50).b));
    }

    SUBCASE("Main shape is on top of glow") {
        // Red circle, blue glow — center pixel should be red (not blue)
        auto fb = render_single(100, 100, [](SceneBuilder& s) {
            s.circle("c", {50, 50, 0}, 20.0f, Color::red())
             .with_glow(Glow{
                .enabled   = true,
                .radius    = 10.0f,
                .intensity = 1.0f,
                .color     = {0.0f, 0.0f, 1.0f, 1.0f}
             });
        });
        // Center should be red (shape painted over glow)
        CHECK(fb->get_pixel(50, 50).r > 0.9f);
        CHECK(fb->get_pixel(50, 50).b < 0.1f);
    }
}

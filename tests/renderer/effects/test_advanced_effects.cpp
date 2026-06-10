#include <doctest/doctest.h>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

using namespace chronon3d;

static std::shared_ptr<Framebuffer> render_advanced_effect_fn(
    std::function<Scene(const FrameContext&)> fn, int w = 64, int h = 64)
{
    SoftwareRenderer rend;
    Composition comp(CompositionSpec{.width=w,.height=h,.duration=1}, fn);
    return rend.render_frame(comp, 0);
}

TEST_CASE("Advanced Effects: Layer Drop Shadow modifies pixels outside shape") {
    auto fb = render_advanced_effect_fn([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("l", [](LayerBuilder& l) {
            // Box is 20x20 in the center
            l.position({0, 0, 0});
            l.rect("r", {.size={20, 20}, .color=Color::white()});
            // Large shadow offset 10 to the right and down
            l.drop_shadow({10, 10}, Color{1.0f, 0.0f, 0.0f, 1.0f}, 0.0f);
        });
        return s.build();
    });

    REQUIRE(fb != nullptr);
    // Center should be white
    CHECK(fb->get_pixel(32, 32).r == doctest::Approx(1.0f));
    CHECK(fb->get_pixel(32, 32).g == doctest::Approx(1.0f));
    CHECK(fb->get_pixel(32, 32).b == doctest::Approx(1.0f));

    // Shadow offset 10 to the right and down: center was 32,32. 
    // Right bound of box is 32+10=42. Drop shadow should color pixel at 45,45 red.
    Color shadow_pixel = fb->get_pixel(45, 45);
    CHECK(shadow_pixel.r > 0.5f);
    CHECK(shadow_pixel.g < 0.1f);
    CHECK(shadow_pixel.b < 0.1f);
}

TEST_CASE("Advanced Effects: Layer Glow modifies pixels outside shape") {
    auto fb = render_advanced_effect_fn([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("l", [](LayerBuilder& l) {
            l.position({0, 0, 0});
            l.rect("r", {.size={20, 20}, .color=Color::white()});
            // Glow with 10px radius, green color
            l.glow(GlowParams{.radius = 10.0f, .intensity = 1.0f, .color = Color::green()});
        });
        return s.build();
    });

    REQUIRE(fb != nullptr);
    // Outside the box, e.g. at 45, 32, we should see green glow
    Color glow_pixel = fb->get_pixel(45, 32);
    CHECK(glow_pixel.g > 0.01f);
    CHECK(glow_pixel.r < 0.01f);
    CHECK(glow_pixel.b < 0.01f);
}

TEST_CASE("Advanced Effects: Layer Bloom highlights bright pixels and spreads") {
    auto fb = render_advanced_effect_fn([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("l", [](LayerBuilder& l) {
            l.position({0, 0, 0});
            l.rect("r", {.size={20, 20}, .color=Color{2.0f, 0.0f, 0.0f, 1.0f}}); // Super bright red
            // Bloom threshold 1.0, so only >1.0 pixels bloom
            l.bloom(1.0f, 8.0f, 1.0f);
        });
        return s.build();
    });

    REQUIRE(fb != nullptr);
    // Outside the box, e.g. at 45, 32, we should see red bloom
    Color bloom_pixel = fb->get_pixel(45, 32);
    CHECK(bloom_pixel.r > 0.01f);
    CHECK(bloom_pixel.g < 0.01f);
    CHECK(bloom_pixel.b < 0.01f);
}

TEST_CASE("Advanced Nodes: MaskNode restricts rendering to mask shape") {
    auto fb = render_advanced_effect_fn([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("l", [](LayerBuilder& l) {
            l.position({0, 0, 0});
            // Large white box
            l.rect("r", {.size={64, 64}, .color=Color::white()});
            // Mask to a small 20x20 area
            l.mask_rect({.size={20, 20}});
        });
        return s.build();
    });

    REQUIRE(fb != nullptr);
    // Center is inside mask
    CHECK(fb->get_pixel(32, 32).r == doctest::Approx(1.0f));
    
    // Pixel 10,10 is outside the 20x20 mask (which is centered at 32,32, bounds 22..42)
    Color outside = fb->get_pixel(10, 10);
    CHECK(outside.a == doctest::Approx(0.0f));
}

TEST_CASE("Universal Glow: preserves original center color") {
    auto fb = render_advanced_effect_fn([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("l", [](LayerBuilder& l) {
            l.position({0, 0, 0});
            // Opaque Red rect of size 20x20
            l.rect("r", {.size={20, 20}, .color=Color::red()});
            // Large bright blue glow with high intensity
            l.glow(GlowParams{.radius = 10.0f, .intensity = 1.0f, .color = Color::blue()});
        });
        return s.build();
    });

    REQUIRE(fb != nullptr);
    // Center pixel (32, 32) must be purely red and unmodified
    Color center = fb->get_pixel(32, 32);
    CHECK(center.r == doctest::Approx(1.0f));
    CHECK(center.g == doctest::Approx(0.0f));
    CHECK(center.b == doctest::Approx(0.0f));
    CHECK(center.a == doctest::Approx(1.0f));

    // Outer pixel (45, 32) must be blue (since glow spreads there)
    Color outer = fb->get_pixel(45, 32);
    CHECK(outer.b > 0.01f);
    CHECK(outer.r == doctest::Approx(0.0f));
}

TEST_CASE("Universal Glow: works on Text layers") {
    auto fb = render_advanced_effect_fn([](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.layer("l", [](LayerBuilder& l) {
            l.position({0, 0, 0});
            l.text("t", {
                .text = "GLOW",
                .size = {30, 20},
                .font_size = 14.0f,
                .color = Color::white(),
                .align = TextAlign::Center
            });
            l.glow(GlowParams{.radius = 8.0f, .intensity = 1.0f, .color = Color::green()});
        });
        return s.build();
    });

    REQUIRE(fb != nullptr);
    // There should be some green glow around the text
    bool found_green_glow = false;
    for (int y = 0; y < fb->height(); ++y) {
        for (int x = 0; x < fb->width(); ++x) {
            Color c = fb->get_pixel(x, y);
            // Look for pixels with low or no red/blue but significant green and alpha
            if (c.g > 0.1f && c.r < 0.1f && c.b < 0.1f) {
                found_green_glow = true;
                break;
            }
        }
        if (found_green_glow) break;
    }
    CHECK(found_green_glow);
}


#include <doctest/doctest.h>
#include <xxhash.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/framebuffer_analysis.hpp>
#include <tests/helpers/render_fixtures.hpp>

#include <algorithm>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

u64 framebuffer_hash(const Framebuffer& fb) {
    return XXH64(fb.pixels_row(0), fb.size_bytes(), 0);
}

float average_luma_rect(const Framebuffer& fb, int x0, int y0, int x1, int y1) {
    double sum = 0.0;
    int count = 0;
    for (int y = std::max(0, y0); y < std::min(fb.height(), y1); ++y) {
        for (int x = std::max(0, x0); x < std::min(fb.width(), x1); ++x) {
            const Color c = fb.get_pixel(x, y);
            if (c.a <= 0.0f) continue;
            sum += 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(sum / count) : 0.0f;
}

// Scene with a light-gray floor (accepts_shadows) and a red card above (casts_shadows).
Composition make_shadow_scene(bool casts, bool accepts,
                               Vec3 light_dir = {-0.4f, 1.0f, -0.6f}) {
    return composition({
        .name = "ShadowScene",
        .width = 320,
        .height = 240,
        .duration = 1
    }, [casts, accepts, light_dir](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable(true).position({0, 0, -800}).zoom(800).look_at({0, 0, 0});
        s.ambient_light(Color{1, 1, 1, 1}, 0.15f);
        s.directional_light(light_dir, Color{1, 1, 1, 1}, 0.85f);

        s.layer("floor", [accepts](LayerBuilder& l) {
            l.enable_3d().position({0, 60, 150}).accepts_shadows(accepts);
            l.rect("fill", {.size = {280, 100}, .color = {0.8f, 0.8f, 0.8f, 1}});
        });

        s.layer("card", [casts](LayerBuilder& l) {
            l.enable_3d().position({0, -20, 0}).casts_shadows(casts);
            l.rect("fill", {.size = {80, 80}, .color = {1, 0.2f, 0.1f, 1}});
        });

        return s.build();
    });
}

} // namespace

TEST_CASE("Shadow builder: casts_shadows sets material flag") {
    LayerBuilder lb("test");
    lb.enable_3d().casts_shadows(true);
    Layer l = lb.build();
    CHECK(l.material.casts_shadows == true);
}

TEST_CASE("Shadow builder: accepts_shadows sets material flag") {
    LayerBuilder lb("test");
    lb.enable_3d().accepts_shadows(false);
    Layer l = lb.build();
    CHECK(l.material.accepts_shadows == false);
}

// ──────────────────────────────────────────────────────────────
// Integration tests
// ──────────────────────────────────────────────────────────────

TEST_CASE("Shadow: visible on receiver — floor luma lower with shadow") {
    auto with_shadow    = render_modular(make_shadow_scene(true,  true));
    auto without_shadow = render_modular(make_shadow_scene(false, true));
    REQUIRE(with_shadow    != nullptr);
    REQUIRE(without_shadow != nullptr);

    save_debug(*with_shadow,    "output/debug/lighting/shadows/01_with_shadow.png");
    save_debug(*without_shadow, "output/debug/lighting/shadows/02_no_shadow.png");

    const float luma_with    = average_luma_rect(*with_shadow,    0, 130, 320, 240);
    const float luma_without = average_luma_rect(*without_shadow, 0, 130, 320, 240);

    CHECK(luma_with < luma_without);
    CHECK(framebuffer_hash(*with_shadow) != framebuffer_hash(*without_shadow));
}

TEST_CASE("Shadow: casts_shadows=false disables shadow") {
    auto with_shadow    = render_modular(make_shadow_scene(true,  true));
    auto casts_disabled = render_modular(make_shadow_scene(false, true));
    REQUIRE(with_shadow    != nullptr);
    REQUIRE(casts_disabled != nullptr);

    save_debug(*casts_disabled, "output/debug/lighting/shadows/03_casts_false.png");

    CHECK(framebuffer_hash(*with_shadow) != framebuffer_hash(*casts_disabled));
}

TEST_CASE("Shadow: accepts_shadows=false disables shadow") {
    auto with_shadow      = render_modular(make_shadow_scene(true, true));
    auto accepts_disabled = render_modular(make_shadow_scene(true, false));
    REQUIRE(with_shadow      != nullptr);
    REQUIRE(accepts_disabled != nullptr);

    save_debug(*accepts_disabled, "output/debug/lighting/shadows/04_accepts_false.png");

    CHECK(framebuffer_hash(*with_shadow) != framebuffer_hash(*accepts_disabled));
}

TEST_CASE("Shadow: light direction changes shadow position/hash") {
    auto light_left  = render_modular(make_shadow_scene(true, true, {-0.8f, 1.0f, -0.3f}));
    auto light_right = render_modular(make_shadow_scene(true, true, { 0.8f, 1.0f, -0.3f}));
    REQUIRE(light_left  != nullptr);
    REQUIRE(light_right != nullptr);

    save_debug(*light_left,  "output/debug/lighting/shadows/05_light_left.png");
    save_debug(*light_right, "output/debug/lighting/shadows/06_light_right.png");

    CHECK(framebuffer_hash(*light_left) != framebuffer_hash(*light_right));
}

TEST_CASE("Shadow: near-zero light.y is clamped and does not crash or produce NaN") {
    auto comp = composition({
        .name = "ShadowNearZeroY", .width = 320, .height = 240, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable(true).position({0, 0, -800}).zoom(800).look_at({0, 0, 0});
        s.ambient_light(Color{1,1,1,1}, 0.15f);
        s.directional_light({1.0f, 0.0001f, 0.0f}, Color{1,1,1,1}, 0.85f);
        s.layer("floor", [](LayerBuilder& l) {
            l.enable_3d().position({0, 60, 150}).accepts_shadows(true);
            l.rect("fill", {.size = {280, 100}, .color = {0.8f, 0.8f, 0.8f, 1}});
        });
        s.layer("card", [](LayerBuilder& l) {
            l.enable_3d().position({0, -20, 0}).casts_shadows(true);
            l.rect("fill", {.size = {80, 80}, .color = {1, 0.2f, 0.1f, 1}});
        });
        return s.build();
    });

    auto fb = render_modular(comp);
    REQUIRE(fb != nullptr);

    save_debug(*fb, "output/debug/lighting/shadows/07_near_zero_light_y.png");

    bool has_nan = false;
    for (int y = 0; y < fb->height() && !has_nan; ++y) {
        const Color* row = fb->pixels_row(y);
        for (int x = 0; x < fb->width() && !has_nan; ++x) {
            has_nan = std::isnan(row[x].r) || std::isnan(row[x].g) ||
                      std::isnan(row[x].b) || std::isnan(row[x].a);
        }
    }
    CHECK(!has_nan);
}

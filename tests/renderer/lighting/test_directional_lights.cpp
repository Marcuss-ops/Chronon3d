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
            if (c.a <= 0.0f) {
                continue;
            }
            sum += 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(sum / count) : 0.0f;
}

Composition make_two_card_scene(bool enable_lights, bool accept_lights, f32 rotated_y) {
    return composition({
        .name = "LightingTwoCards",
        .width = 640,
        .height = 360,
        .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable(true).position({0, 0, -1000}).zoom(1000).look_at({0, 0, 0});
        if (enable_lights) {
            s.ambient_light(Color{1, 1, 1, 1}, 0.15f);
            s.directional_light({0, 0, -1}, Color{1, 1, 1, 1}, 0.85f);
        }

        s.layer("left", [=](LayerBuilder& l) {
            l.enable_3d()
             .accepts_lights(accept_lights)
             .position({-140, 0, 0});
            l.rect("fill", {
                .size = {120, 120},
                .color = Color{1, 1, 1, 1},
                .pos = {0, 0, 0}
            });
        });

        s.layer("right", [=](LayerBuilder& l) {
            l.enable_3d()
             .accepts_lights(accept_lights)
             .position({140, 0, 0})
             .rotate({0, rotated_y, 0});
            l.rect("fill", {
                .size = {120, 120},
                .color = Color{1, 1, 1, 1},
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });
}

Composition make_parented_card_scene(f32 parent_y, bool enable_lights = true) {
    return composition({
        .name = "LightingParentedCard",
        .width = 640,
        .height = 360,
        .duration = 1
    }, [=](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().enable(true).position({0, 0, -1000}).zoom(1000).look_at({0, 0, 0});
        if (enable_lights) {
            s.ambient_light(Color{1, 1, 1, 1}, 0.15f);
            s.directional_light({0, 0, -1}, Color{1, 1, 1, 1}, 0.85f);
        }

        s.null_layer("rig", [=](LayerBuilder& l) {
            l.enable_3d().rotate({0, parent_y, 0});
        });

        s.layer("card", [](LayerBuilder& l) {
            l.parent("rig")
             .enable_3d()
             .position({0, 0, 0});
            l.rect("fill", {
                .size = {120, 120},
                .color = Color{1, 1, 1, 1},
                .pos = {0, 0, 0}
            });
        });

        return s.build();
    });
}

} // namespace

TEST_CASE("Lighting visual: facing card is brighter than rotated card") {
    auto fb = render_modular(make_two_card_scene(true, true, 70.0f));
    REQUIRE(fb != nullptr);

    save_debug(*fb, "output/debug/lighting/01_facing_vs_rotated.png");

    const float left_luma = average_luma_rect(*fb, 70, 80, 260, 280);
    const float right_luma = average_luma_rect(*fb, 380, 80, 570, 280);

    CHECK(left_luma > right_luma);
}

TEST_CASE("Lighting visual: parent rotation changes child lighting") {
    auto flat = render_modular(make_parented_card_scene(0.0f));
    auto rotated = render_modular(make_parented_card_scene(70.0f));
    REQUIRE(flat != nullptr);
    REQUIRE(rotated != nullptr);

    save_debug(*flat, "output/debug/lighting/02_parent_flat.png");
    save_debug(*rotated, "output/debug/lighting/03_parent_rotated.png");

    const float flat_luma = average_luma_rect(*flat, 220, 80, 420, 280);
    const float rotated_luma = average_luma_rect(*rotated, 220, 80, 420, 280);

    CHECK(flat_luma > rotated_luma);
}

TEST_CASE("Lighting visual: accepts_lights=false matches no-light output") {
    auto no_lights = render_modular(make_two_card_scene(false, false, 70.0f));
    auto disabled = render_modular(make_two_card_scene(true, false, 70.0f));
    REQUIRE(no_lights != nullptr);
    REQUIRE(disabled != nullptr);

    save_debug(*no_lights, "output/debug/lighting/04_no_lights.png");
    save_debug(*disabled, "output/debug/lighting/05_disabled_material.png");

    CHECK(framebuffer_hash(*no_lights) == framebuffer_hash(*disabled));
}

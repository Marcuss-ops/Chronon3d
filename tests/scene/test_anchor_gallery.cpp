#include <doctest/doctest.h>

#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/layout/layout_solver.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

#include <array>
#include <filesystem>

using namespace chronon3d;

namespace {

struct AnchorCase {
    const char* name;
    AnchorPlacement placement;
    f32 margin;
    Color color;
};

f32 pixel_delta(const Color& a, const Color& b) {
    return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b) + std::abs(a.a - b.a);
}

static const std::array<AnchorCase, 9> kAnchorCases{{
    {"top_left", AnchorPlacement{Anchor::TopLeft}, 48.0f, Color::from_hex("#f2f2f2")},
    {"top_center", AnchorPlacement{Anchor::TopCenter}, 48.0f, Color::from_hex("#fff1a8")},
    {"top_right", AnchorPlacement{Anchor::TopRight}, 48.0f, Color::from_hex("#bde0fe")},
    {"middle_left", AnchorPlacement{Anchor::MiddleLeft}, 48.0f, Color::from_hex("#ffc8dd")},
    {"center", AnchorPlacement{Anchor::Center}, 0.0f, Color::from_hex("#e7c6ff")},
    {"middle_right", AnchorPlacement{Anchor::MiddleRight}, 48.0f, Color::from_hex("#caffbf")},
    {"bottom_left", AnchorPlacement{Anchor::BottomLeft}, 48.0f, Color::from_hex("#ffd6a5")},
    {"bottom_center", AnchorPlacement{Anchor::BottomCenter}, 48.0f, Color::from_hex("#a0c4ff")},
    {"bottom_right", AnchorPlacement{Anchor::BottomRight}, 48.0f, Color::from_hex("#fdffb6")},
}};

void build_anchor_scene(SceneBuilder& s, const FrameContext& ctx, const AnchorCase& c) {
    s.layer("background", [ctx](LayerBuilder& l) {
        l.rect("bg", {
            .size = {static_cast<f32>(ctx.width), static_cast<f32>(ctx.height)},
            .color = Color::from_hex("#1b1d22"),
            .pos = {static_cast<f32>(ctx.width) * 0.5f, static_cast<f32>(ctx.height) * 0.5f, 0.0f},
        });
    });

    s.layer(std::string{"card_"} + c.name, [c](LayerBuilder& l) {
        l.pin_to(c.placement, c.margin)
         .rounded_rect("card", {
             .size = {64.0f, 64.0f},
             .radius = 14.0f,
             .color = c.color,
             .pos = {0.0f, 0.0f, 0.0f},
         });
    });
}

} // namespace

TEST_CASE("Anchor gallery renders PNGs for all anchor presets") {
    constexpr i32 W = 1280;
    constexpr i32 H = 720;

    std::filesystem::path out_dir = "output/layout/anchors";
    std::filesystem::create_directories(out_dir);

    for (const auto& c : kAnchorCases) {
        FrameContext ctx;
        ctx.width = W;
        ctx.height = H;

        SceneBuilder builder(ctx);
        build_anchor_scene(builder, ctx, c);

        Scene scene = builder.build();
        LayoutSolver{}.solve(scene, W, H);

        SoftwareRenderer renderer;
        auto fb = renderer.render_scene(scene, std::nullopt, W, H);
        REQUIRE(fb != nullptr);
        CHECK(fb->width() == W);
        CHECK(fb->height() == H);

        Vec2 expected = anchor_position(c.placement.anchor, W, H, c.margin);
        const Color center = fb->get_pixel(static_cast<i32>(expected.x), static_cast<i32>(expected.y));
        const Color background = fb->get_pixel(10, 10);
        CHECK(center.a > 0.9f);
        CHECK(pixel_delta(center, background) > 0.05f);

        const std::filesystem::path out = out_dir / (std::string{"anchor_"} + c.name + ".png");
        CHECK(save_png(*fb, out.string()));
        CHECK(std::filesystem::exists(out));
        CHECK(std::filesystem::file_size(out) > 0);
    }
}

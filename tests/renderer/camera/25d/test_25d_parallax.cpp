#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <filesystem>
#include <cmath>

using namespace chronon3d;

namespace {

std::unique_ptr<Framebuffer> render25d(const Composition& comp) {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    return renderer.render_frame(comp, 0);
}

// Centroid X of all pixels matching the color selector across the entire image.
// Returns -1 if none found.
float color_centroid_x(const Framebuffer& fb,
                       bool is_red, bool is_green, bool is_blue) {
    float sum_x = 0.0f; int count = 0;
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            auto p = fb.get_pixel(x, y);
            if (p.a < 0.05f) continue;
            bool match = false;
            if (is_red   && p.r > 0.4f && p.g < 0.3f && p.b < 0.3f) match = true;
            if (is_green && p.g > 0.4f && p.r < 0.3f && p.b < 0.3f) match = true;
            if (is_blue  && p.b > 0.4f && p.r < 0.3f && p.g < 0.3f) match = true;
            if (match) { sum_x += static_cast<float>(x); ++count; }
        }
    }
    return count > 0 ? sum_x / static_cast<float>(count) : -1.0f;
}

int count_colored_pixels(const Framebuffer& fb, bool is_red, bool is_blue) {
    int n = 0;
    for (int y = 0; y < fb.height(); ++y)
        for (int x = 0; x < fb.width(); ++x) {
            auto p = fb.get_pixel(x, y);
            if (p.a < 0.05f) continue;
            if (is_red  && p.r > 0.4f && p.g < 0.3f && p.b < 0.3f) ++n;
            if (is_blue && p.b > 0.4f && p.r < 0.3f && p.g < 0.3f) ++n;
        }
    return n;
}

} // namespace

// ---------------------------------------------------------------------------
// Layer positions are in canvas-centered space: {0,0,0} == middle of frame.
// ---------------------------------------------------------------------------

TEST_CASE("2.5D parallax: near layer shifts more than far layer on camera pan") {
    // Near red (Z=-400) and far blue (Z=600).
    // Layers are placed at different Y so they don't overlap.
    // Camera pans right (+X=80). In projection, near shifts left more than far.

    const int W = 400, H = 300;
    const float cam_zoom = 1000.0f;

    auto make_comp = [&](float cam_x) {
        return composition({
            .name = "ParallaxTest", .width = W, .height = H, .duration = 1
        }, [=](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().set({
                .enabled  = true,
                .position = {cam_x, 0, -cam_zoom},
                .zoom     = cam_zoom
            });
            // Y offsets separate the layers visually.
            s.layer("near_red", [](LayerBuilder& l) {
                l.enable_3d().position({0, -60.0f, -400.0f});
                l.rect("r", {.size={40,40}, .color=Color{1,0,0,1}, .pos={0,0,0}});
            });
            s.layer("far_blue", [](LayerBuilder& l) {
                l.enable_3d().position({0, 60.0f, 600.0f});
                l.rect("b", {.size={40,40}, .color=Color{0,0,1,1}, .pos={0,0,0}});
            });
            return s.build();
        });
    };

    auto fb_center = render25d(make_comp(0.0f));
    auto fb_pan    = render25d(make_comp(80.0f));
    REQUIRE(fb_center != nullptr);
    REQUIRE(fb_pan    != nullptr);

    float red_c  = color_centroid_x(*fb_center, true,  false, false);
    float blue_c = color_centroid_x(*fb_center, false, false, true);
    float red_p  = color_centroid_x(*fb_pan,    true,  false, false);
    float blue_p = color_centroid_x(*fb_pan,    false, false, true);

    // Layers must be visible in both frames.
    CHECK(red_c  >= 0.0f);
    CHECK(blue_c >= 0.0f);
    CHECK(red_p  >= 0.0f);
    CHECK(blue_p >= 0.0f);

    float delta_near = std::abs(red_p  - red_c);
    float delta_far  = std::abs(blue_p - blue_c);

    // Parallax: near shifts more than far when camera pans.
    CHECK(delta_near > delta_far);

    std::filesystem::create_directories("output/debug/25d");
    save_png(*fb_center, "output/debug/25d/parallax_center.png");
    save_png(*fb_pan,    "output/debug/25d/parallax_panned.png");
}

// ---------------------------------------------------------------------------
TEST_CASE("2.5D z-order: near layer covers far layer regardless of insertion order") {
    // Near red (Z=-300) inserted first, far blue (Z=500) inserted second.
    // After depth sort: far renders first, near paints on top at center.

    auto comp = composition({
        .name = "ZOrderVisual", .width = 300, .height = 300, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().set({
            .enabled = true, .position = {0, 0, -1000}, .zoom = 1000.0f
        });
        s.layer("near_red", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, -300.0f});
            l.rect("r", {.size={80,80}, .color=Color{1,0,0,1}, .pos={0,0,0}});
        });
        s.layer("far_blue", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, 500.0f});
            l.rect("b", {.size={200,200}, .color=Color{0,0,1,1}, .pos={0,0,0}});
        });
        return s.build();
    });

    auto fb = render25d(comp);
    REQUIRE(fb != nullptr);

    // Canvas center in a 300×300 frame is (150, 150).
    auto center = fb->get_pixel(150, 150);
    CHECK(center.r > 0.6f);
    CHECK(center.b < 0.4f);

    std::filesystem::create_directories("output/debug/25d");
    save_png(*fb, "output/debug/25d/z_order.png");
}

// ---------------------------------------------------------------------------
TEST_CASE("2.5D behind-camera: culled layer leaves no pixels") {
    // Layer at Z=-1200 is behind camera at Z=-1000. Must not paint.

    auto comp = composition({
        .name = "BehindCameraVisual", .width = 200, .height = 200, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().set({
            .enabled = true, .position = {0, 0, -1000}, .zoom = 1000.0f
        });
        // Dark BG so any stray magenta pixel is detectable.
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("dark", {.size={200,200}, .color=Color{0.1f,0.1f,0.1f,1}, .pos={0,0,0}});
        });
        s.layer("culled", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, -1200.0f});
            l.rect("m", {.size={150,150}, .color=Color{1,0,1,1}, .pos={0,0,0}});
        });
        return s.build();
    });

    auto fb = render25d(comp);
    REQUIRE(fb != nullptr);

    bool found_magenta = false;
    for (int y = 0; y < fb->height() && !found_magenta; ++y)
        for (int x = 0; x < fb->width() && !found_magenta; ++x) {
            auto p = fb->get_pixel(x, y);
            if (p.r > 0.5f && p.g < 0.2f && p.b > 0.5f) found_magenta = true;
        }
    CHECK(!found_magenta);

    std::filesystem::create_directories("output/debug/25d");
    save_png(*fb, "output/debug/25d/behind_camera.png");
}

// ---------------------------------------------------------------------------
TEST_CASE("2.5D dolly: near card grows faster than far card when camera dollies in") {
    // Camera moves from Z=-2000 to Z=-500. Near card (Z=-100) grows more
    // than far card (Z=800). Tested via pixel count per color.

    const int W = 400, H = 300;

    // Separate compositions: one rect per render to avoid overlap.
    auto make_near = [&](float cam_z) {
        return composition({
            .name="DollyNear", .width=W, .height=H, .duration=1
        }, [cam_z](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().set({
                .enabled=true, .position={0,0,cam_z}, .zoom=std::abs(cam_z)
            });
            s.layer("red", [](LayerBuilder& l) {
                l.enable_3d().position({0, 0, -100.0f});
                l.rect("r", {.size={60,60}, .color=Color{1,0,0,1}, .pos={0,0,0}});
            });
            return s.build();
        });
    };
    auto make_far = [&](float cam_z) {
        return composition({
            .name="DollyFar", .width=W, .height=H, .duration=1
        }, [cam_z](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.camera().set({
                .enabled=true, .position={0,0,cam_z}, .zoom=std::abs(cam_z)
            });
            s.layer("blue", [](LayerBuilder& l) {
                l.enable_3d().position({0, 0, 800.0f});
                l.rect("b", {.size={60,60}, .color=Color{0,0,1,1}, .pos={0,0,0}});
            });
            return s.build();
        });
    };

    auto fb_near_far  = render25d(make_near(-2000.0f));
    auto fb_near_near = render25d(make_near( -500.0f));
    auto fb_far_far   = render25d(make_far (-2000.0f));
    auto fb_far_near  = render25d(make_far ( -500.0f));

    int red_at_cam_far  = count_colored_pixels(*fb_near_far,  true, false);
    int red_at_cam_near = count_colored_pixels(*fb_near_near, true, false);
    int blu_at_cam_far  = count_colored_pixels(*fb_far_far,   false, true);
    int blu_at_cam_near = count_colored_pixels(*fb_far_near,  false, true);

    // Near card grows more on dolly-in.
    float red_growth = red_at_cam_near > 0 && red_at_cam_far > 0
        ? static_cast<float>(red_at_cam_near) / static_cast<float>(red_at_cam_far) : 0.0f;
    float blu_growth = blu_at_cam_near > 0 && blu_at_cam_far > 0
        ? static_cast<float>(blu_at_cam_near) / static_cast<float>(blu_at_cam_far) : 0.0f;

    CHECK(red_at_cam_far  > 0);
    CHECK(red_at_cam_near > 0);
    CHECK(blu_at_cam_far  > 0);
    CHECK(blu_at_cam_near > 0);
    CHECK(red_growth > blu_growth);

    std::filesystem::create_directories("output/debug/25d");
    save_png(*fb_near_far,  "output/debug/25d/dolly_near_cam_far.png");
    save_png(*fb_near_near, "output/debug/25d/dolly_near_cam_near.png");
    save_png(*fb_far_far,   "output/debug/25d/dolly_far_cam_far.png");
    save_png(*fb_far_near,  "output/debug/25d/dolly_far_cam_near.png");
}

#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <filesystem>
#include <cmath>
#include <limits>

using namespace chronon3d;

namespace {

std::unique_ptr<Framebuffer> render25d(const Composition& comp) {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    return renderer.render_frame(comp, 0);
}

struct ColorStats {
    float centroid_x{-1};
    float centroid_y{-1};
    int pixel_count{0};
};

ColorStats red_stats(const Framebuffer& fb) {
    float sum_x = 0, sum_y = 0; int n = 0;
    for (int y = 0; y < fb.height(); ++y)
        for (int x = 0; x < fb.width(); ++x) {
            auto p = fb.get_pixel(x, y);
            if (p.r > 0.4f && p.g < 0.3f && p.b < 0.3f && p.a > 0.05f) {
                sum_x += x; sum_y += y; ++n;
            }
        }
    if (n == 0) return {};
    return {sum_x / n, sum_y / n, n};
}

// Visible width of red pixels at a given screen row.
int red_width_at_row(const Framebuffer& fb, int y) {
    int first = -1, last = -1;
    for (int x = 0; x < fb.width(); ++x) {
        auto p = fb.get_pixel(x, y);
        if (p.r > 0.4f && p.g < 0.3f && p.b < 0.3f && p.a > 0.05f) {
            if (first < 0) first = x;
            last = x;
        }
    }
    return (first >= 0) ? (last - first + 1) : 0;
}

// Visible height of red pixels at a given screen column.
int red_height_at_col(const Framebuffer& fb, int x) {
    int first = -1, last = -1;
    for (int y = 0; y < fb.height(); ++y) {
        auto p = fb.get_pixel(x, y);
        if (p.r > 0.4f && p.g < 0.3f && p.b < 0.3f && p.a > 0.05f) {
            if (first < 0) first = y;
            last = y;
        }
    }
    return (first >= 0) ? (last - first + 1) : 0;
}

int count_red(const Framebuffer& fb) {
    int n = 0;
    for (int y = 0; y < fb.height(); ++y)
        for (int x = 0; x < fb.width(); ++x) {
            auto p = fb.get_pixel(x, y);
            if (p.r > 0.4f && p.g < 0.3f && p.b < 0.3f && p.a > 0.05f) ++n;
        }
    return n;
}

} // namespace

// ---------------------------------------------------------------------------
// Camera pan (rotation.y): layer at origin shifts LEFT in screen space.
// With 10° pan: centroid shifts ~180px from canvas center.
// ---------------------------------------------------------------------------
TEST_CASE("2.5D camera pan: layer shifts horizontally when camera rotates around Y") {
    const int W = 640, H = 480;

    auto make_comp = [&](float rot_y_deg) {
        return composition({
            .name = "CamPanTest", .width = W, .height = H, .duration = 1
        }, [rot_y_deg](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            Camera2_5D cam;
            cam.enabled  = true;
            cam.position = {0, 0, -1000};
            cam.zoom     = 1000.0f;
            cam.set_pan(rot_y_deg);
            s.camera().set(cam);
            // 100×100 rect centered at layer origin → modular mode centers at canvas center.
            s.layer("rect", [](LayerBuilder& l) {
                l.enable_3d().position({0, 0, 0.0f});
                l.rect("r", {.size={100,100}, .color=Color{1,0,0,1}, .pos={0,0,0}});
            });
            return s.build();
        });
    };

    auto fb_straight = render25d(make_comp(0.0f));
    auto fb_panned   = render25d(make_comp(10.0f));
    REQUIRE(fb_straight != nullptr);
    REQUIRE(fb_panned   != nullptr);

    auto st = red_stats(*fb_straight);
    auto pn = red_stats(*fb_panned);

    // Straight camera: rect visible at canvas center.
    CHECK(st.pixel_count > 100);
    CHECK(std::abs(st.centroid_x - W * 0.5f) < 20.0f);

    // Panned camera: rect shifts significantly from center (camera looks right → layer goes left).
    CHECK(pn.pixel_count > 0);
    // Centroid should move at least 50px from center for a 10° pan.
    CHECK(std::abs(pn.centroid_x - W * 0.5f) > 50.0f);

    std::filesystem::create_directories("output/debug/25d");
    save_png(*fb_straight, "output/debug/25d/cam_pan_straight.png");
    save_png(*fb_panned,   "output/debug/25d/cam_pan_panned.png");
}

// ---------------------------------------------------------------------------
// Camera tilt (rotation.x): layer at origin shifts vertically in screen space.
// With 10° tilt: centroid shifts ~180px from canvas center.
// ---------------------------------------------------------------------------
TEST_CASE("2.5D camera tilt: layer shifts vertically when camera rotates around X") {
    const int W = 640, H = 480;

    auto make_comp = [&](float rot_x_deg) {
        return composition({
            .name = "CamTiltTest", .width = W, .height = H, .duration = 1
        }, [rot_x_deg](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            Camera2_5D cam;
            cam.enabled  = true;
            cam.position = {0, 0, -1000};
            cam.zoom     = 1000.0f;
            cam.set_tilt(rot_x_deg);
            s.camera().set(cam);
            s.layer("rect", [](LayerBuilder& l) {
                l.enable_3d().position({0, 0, 0.0f});
                l.rect("r", {.size={100,100}, .color=Color{1,0,0,1}, .pos={0,0,0}});
            });
            return s.build();
        });
    };

    auto fb_straight = render25d(make_comp(0.0f));
    auto fb_tilted   = render25d(make_comp(10.0f));
    REQUIRE(fb_straight != nullptr);
    REQUIRE(fb_tilted   != nullptr);

    auto st = red_stats(*fb_straight);
    auto ti = red_stats(*fb_tilted);

    CHECK(st.pixel_count > 100);
    CHECK(std::abs(st.centroid_x - W * 0.5f) < 20.0f);
    CHECK(std::abs(st.centroid_y - H * 0.5f) < 20.0f);

    CHECK(ti.pixel_count > 0);
    // Centroid shifts at least 50px in Y for a 10° tilt.
    CHECK(std::abs(ti.centroid_y - H * 0.5f) > 50.0f);

    std::filesystem::create_directories("output/debug/25d");
    save_png(*fb_straight, "output/debug/25d/cam_tilt_straight.png");
    save_png(*fb_tilted,   "output/debug/25d/cam_tilt_tilted.png");
}

// ---------------------------------------------------------------------------
// Trapezoid test with POI camera: flat rect appears wider at the bottom when
// camera looks down from above (POI at origin, camera above center).
// Uses point_of_interest so the layer stays centered on screen.
// ---------------------------------------------------------------------------
TEST_CASE("2.5D perspective trapezoid: flat rect wider at near edge with POI tilt") {
    const int W = 640, H = 480;
    const float rect_size = 200.0f;

    // Camera BELOW the scene center (positive Y = down in screen space) looking UP at POI.
    // This places the POI layer toward the top of the camera's view: top of rect is CLOSER,
    // bottom is FARTHER. Near edge (top) appears wider, far edge (bottom) narrower.
    auto make_comp = [&](float cam_y) {
        return composition({
            .name = "TrapezoidTest", .width = W, .height = H, .duration = 1
        }, [cam_y, rect_size](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            Camera2_5D cam;
            cam.enabled                  = true;
            cam.position                 = {0, cam_y, -1000};
            cam.point_of_interest        = {0, 0, 0};
            cam.point_of_interest_enabled = true;
            cam.zoom                     = 1000.0f;
            s.camera().set(cam);
            s.layer("rect", [rect_size](LayerBuilder& l) {
                l.enable_3d().position({0, 0, 0.0f});
                l.rect("r", {.size={rect_size, rect_size}, .color=Color{1,0,0,1}, .pos={0,0,0}});
            });
            return s.build();
        });
    };

    auto fb_straight = render25d(make_comp(0.0f));     // straight: no tilt
    auto fb_tilted   = render25d(make_comp(400.0f));   // camera below center, looking up

    REQUIRE(fb_straight != nullptr);
    REQUIRE(fb_tilted   != nullptr);

    // The straight render must show a symmetric rect centered on screen.
    CHECK(count_red(*fb_straight) > 1000);

    // For tilted: find the vertical bounding box of the rect, then compare
    // widths at 1/4 and 3/4 of that range.
    int min_y = H, max_y = 0;
    for (int y = 0; y < H; ++y)
        if (red_width_at_row(*fb_tilted, y) > 0) {
            min_y = std::min(min_y, y);
            max_y = std::max(max_y, y);
        }

    bool found_rect = (max_y > min_y + 10);
    CHECK(found_rect);

    if (found_rect) {
        int quarter_y     = min_y + (max_y - min_y) / 4;
        int threequart_y  = min_y + 3 * (max_y - min_y) / 4;
        int top_width     = red_width_at_row(*fb_tilted, quarter_y);
        int bot_width     = red_width_at_row(*fb_tilted, threequart_y);

        // Near and far edges should have measurably different widths (trapezoid).
        // We require at least 2px difference for a 200px rect at 400-unit tilt offset.
        CHECK(std::abs(top_width - bot_width) > 2);
    }

    std::filesystem::create_directories("output/debug/25d");
    save_png(*fb_straight, "output/debug/25d/trapezoid_straight.png");
    save_png(*fb_tilted,   "output/debug/25d/trapezoid_tilted.png");
}

// ---------------------------------------------------------------------------
// Perspective scale: near layer is visually larger than far layer.
// ---------------------------------------------------------------------------
TEST_CASE("2.5D perspective scale: near layer occupies more pixels than far layer") {
    const int W = 400, H = 300;

    Camera2_5D cam;
    cam.enabled  = true;
    cam.position = {0, 0, -1000};
    cam.zoom     = 1000.0f;

    auto comp_near = composition({
        .name="PerspNear", .width=W, .height=H, .duration=1
    }, [cam](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().set(cam);
        s.layer("rect", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, -400.0f});
            l.rect("r", {.size={80,80}, .color=Color{1,0,0,1}, .pos={0,0,0}});
        });
        return s.build();
    });
    auto comp_far = composition({
        .name="PerspFar", .width=W, .height=H, .duration=1
    }, [cam](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera().set(cam);
        s.layer("rect", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, 600.0f});
            l.rect("r", {.size={80,80}, .color=Color{1,0,0,1}, .pos={0,0,0}});
        });
        return s.build();
    });

    auto fb_near = render25d(comp_near);
    auto fb_far  = render25d(comp_far);
    REQUIRE(fb_near != nullptr);
    REQUIRE(fb_far  != nullptr);

    int near_px = count_red(*fb_near);
    int far_px  = count_red(*fb_far);

    CHECK(near_px > 0);
    CHECK(far_px  > 0);
    CHECK(near_px > far_px);

    std::filesystem::create_directories("output/debug/25d");
    save_png(*fb_near, "output/debug/25d/perspective_near.png");
    save_png(*fb_far,  "output/debug/25d/perspective_far.png");
}

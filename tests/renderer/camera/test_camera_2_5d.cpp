#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <cmath>

using namespace chronon3d;

// ---------------------------------------------------------------------------
TEST_CASE("Camera 2.5D: perspective_scale at normal depth equals 1") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000.0f;

    Transform t;
    t.position = {640, 360, 0};

    auto p = project_layer_2_5d(t, cam, 1280, 720);

    CHECK(p.perspective_scale == doctest::Approx(1.0f));
}

// ---------------------------------------------------------------------------
TEST_CASE("Camera 2.5D: projection scales by depth") {
    Camera2_5D cam;
    cam.enabled = true;
    cam.position = {0, 0, -1000};
    cam.zoom = 1000.0f;

    Transform normal_t; normal_t.position = {640, 360,    0};
    Transform near_t;   near_t.position   = {640, 360, -500};
    Transform far_t;    far_t.position    = {640, 360,  500};

    auto p_normal = project_layer_2_5d(normal_t, cam, 1280, 720);
    auto p_near   = project_layer_2_5d(near_t,   cam, 1280, 720);
    auto p_far    = project_layer_2_5d(far_t,    cam, 1280, 720);

    CHECK(p_near.perspective_scale   > p_normal.perspective_scale);
    CHECK(p_normal.perspective_scale > p_far.perspective_scale);
    // depth=500 → scale=2; depth=1500 → scale≈0.667
    CHECK(p_near.perspective_scale   == doctest::Approx(2.0f));
    CHECK(p_far.perspective_scale    == doctest::Approx(1000.0f / 1500.0f).epsilon(0.01));
}

// ---------------------------------------------------------------------------
TEST_CASE("Camera 2.5D: near layers shift more than far layers (parallax)") {
    Camera2_5D cam_a;
    cam_a.enabled  = true;
    cam_a.position = {-100, 0, -1000};
    cam_a.zoom     = 1000.0f;

    Camera2_5D cam_b = cam_a;
    cam_b.position.x = 100;

    Transform near_t; near_t.position = {640, 360, -400};
    Transform far_t;  far_t.position  = {640, 360,  600};

    auto near_a = project_layer_2_5d(near_t, cam_a, 1280, 720);
    auto near_b = project_layer_2_5d(near_t, cam_b, 1280, 720);
    auto far_a  = project_layer_2_5d(far_t,  cam_a, 1280, 720);
    auto far_b  = project_layer_2_5d(far_t,  cam_b, 1280, 720);

    f32 near_delta = std::abs(near_b.transform.position.x - near_a.transform.position.x);
    f32 far_delta  = std::abs(far_b.transform.position.x  - far_a.transform.position.x);

    CHECK(near_delta > far_delta);
}

// ---------------------------------------------------------------------------
TEST_CASE("Camera 2.5D: z sorting draws near layer on top of far layer") {
    // Near-red inserted first, far-blue inserted second.
    // After z-sort, far-blue renders first → near-red paints on top.
    Composition comp = composition({
        .name     = "CameraZSortTest",
        .width    = 200,
        .height   = 200,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera().set({
            .enabled          = true,
            .position         = {0, 0, -1000},
            .zoom             = 1000.0f
        });

        s.layer("near-red", [](LayerBuilder& l) {
            l.enable_3d(true)
             .position({0, 0, -500});
            l.rect("red", {
                .size  = {100, 100},
                .color = Color{1, 0, 0, 1},
                .pos   = {0, 0, 0}
            });
        });

        s.layer("far-blue", [](LayerBuilder& l) {
            l.enable_3d(true)
             .position({0, 0, 1000});
            l.rect("blue", {
                .size  = {200, 200},
                .color = Color{0, 0, 1, 1},
                .pos   = {0, 0, 0}
            });
        });

        return s.build();
    });

    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    auto fb = renderer.render_frame(comp, 0);
    auto p  = fb->get_pixel(100, 100);

    CHECK(p.r > 0.5f);
    CHECK(p.b < 0.2f);
}

// ---------------------------------------------------------------------------
TEST_CASE("Camera 2.5D disabled: 2D layers unaffected") {
    // Without camera enabled, layer at z=500 should render at its own position.
    Composition comp = composition({
        .name     = "Camera2_5DDisabledTest",
        .width    = 200,
        .height   = 200,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // camera_2_5d not set → disabled by default

        s.layer("box", [](LayerBuilder& l) {
            l.position({100, 100, 500}); // Z ignored for 2D
            l.rect("r", {
                .size  = {60, 60},
                .color = Color{0, 1, 0, 1},
                .pos   = {0, 0, 0}
            });
        });

        return s.build();
    });

    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(comp, 0);
    auto p  = fb->get_pixel(100, 100);

    CHECK(p.g > 0.5f);
}

// ---------------------------------------------------------------------------
TEST_CASE("Camera 2.5D: 2D layer unaffected when camera enabled") {
    // Layer without enable_3d must not be projected even when camera is active.
    Composition comp = composition({
        .name     = "Camera2_5D2DLayerUnaffectedTest",
        .width    = 400,
        .height   = 400,
        .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera().set({
            .enabled  = true,
            .position = {500, 0, -1000}, // large X offset
            .zoom     = 1000.0f
        });

        // 2D layer centered at (200,200) — must stay there regardless of camera X.
        s.layer("hud", [](LayerBuilder& l) {
            l.position({200, 200, 0}); // no enable_3d
            l.rect("r", {
                .size  = {40, 40},
                .color = Color{1, 1, 0, 1},
                .pos   = {0, 0, 0}
            });
        });

        return s.build();
    });

    SoftwareRenderer renderer;
    auto fb = renderer.render_frame(comp, 0);
    auto p  = fb->get_pixel(200, 200);

    // Yellow rect must be at (200,200), not displaced by camera.
    CHECK(p.r > 0.5f);
    CHECK(p.g > 0.5f);
    CHECK(p.b < 0.1f);
}

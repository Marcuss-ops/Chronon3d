// graph_25d_proofs.cpp
// ─────────────────────────────────────────────────────────────────────────
// Visual proof suite for 2.5D modular RenderGraph verification.
// All tests use center-origin coordinates (0,0 is screen center).
// ─────────────────────────────────────────────────────────────────────────

#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

// 1. Projection test: near big, far small
static Composition camera25d_graph_projection() {
    return composition({ .name = "camera25d_graph_projection", .width = 1280, .height = 720 }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d({ .enabled = true, .position = {0, 0, -1000}, .zoom = 1000.0f });

        s.rect("bg", { .size = {1280, 720}, .color = Color{0.05f, 0.05f, 0.1f, 1.0f}, .pos = {0, 0, 0} });

        s.layer("far", [](LayerBuilder& l) {
            l.enable_3d().position({-350, 0, 1000});
            l.rect("far", { .size = {200, 200}, .color = Color::blue(), .pos = {0, 0, 0} });
        });

        s.layer("subject", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, 0});
            l.rect("subject", { .size = {200, 200}, .color = Color::green(), .pos = {0, 0, 0} });
        });

        s.layer("near", [](LayerBuilder& l) {
            l.enable_3d().position({350, 0, -500});
            l.rect("near", { .size = {200, 200}, .color = Color::red(), .pos = {0, 0, 0} });
        });

        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("camera25d_graph_projection", camera25d_graph_projection)

// 2 & 3. Parallax test: camera pan (frame 0 and 60)
static Composition camera25d_graph_parallax() {
    return composition({ .name = "camera25d_graph_parallax", .width = 1280, .height = 720, .duration = 61 }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 t = static_cast<f32>(ctx.frame) / 60.0f;
        s.camera_2_5d({ .enabled = true, .position = {interpolate(t, 0.0f, 1.0f, 0.0f, 200.0f), 0, -1000}, .zoom = 1000.0f });

        s.rect("bg", { .size = {1280, 720}, .color = Color{0.02f, 0.02f, 0.02f, 1.0f}, .pos = {0, 0, 0} });

        s.layer("bg", [](LayerBuilder& l) {
            l.enable_3d().depth_role(DepthRole::Background);
            l.rect("r", { .size = {300, 300}, .color = Color{0.1f, 0.2f, 1.0f, 0.8f}, .pos = {0, 0, 0} });
        });

        s.layer("sub", [](LayerBuilder& l) {
            l.enable_3d().depth_role(DepthRole::Subject);
            l.rect("r", { .size = {300, 300}, .color = Color{0.1f, 1.0f, 0.2f, 0.8f}, .pos = {0, 0, 0} });
        });

        s.layer("fg", [](LayerBuilder& l) {
            l.enable_3d().depth_role(DepthRole::Foreground);
            l.rect("r", { .size = {300, 300}, .color = Color{1.0f, 0.2f, 0.1f, 0.8f}, .pos = {0, 0, 0} });
        });

        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("camera25d_graph_parallax", camera25d_graph_parallax)

// 4. Z sorting test: nearer red red must cover farther blue even if red is inserted first
static Composition camera25d_graph_z_sorting() {
    return composition({ .name = "camera25d_graph_z_sorting", .width = 1280, .height = 720 }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d({ .enabled = true, .position = {0, 0, -1000}, .zoom = 1000.0f });

        s.rect("bg", { .size = {1280, 720}, .color = Color::black(), .pos = {0, 0, 0} });

        // NEAR (red) inserted first
        s.layer("near_red", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, -500});
            l.rect("red", { .size = {400, 400}, .color = Color::red(), .pos = {0, 0, 0} });
        });

        // FAR (blue) inserted last
        s.layer("far_blue", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, 1000});
            l.rect("blue", { .size = {800, 800}, .color = Color::blue(), .pos = {0, 0, 0} });
        });

        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("camera25d_graph_z_sorting", camera25d_graph_z_sorting)

// 5. 2D overlay test: UI stays on top and fixed
static Composition camera25d_graph_2d_overlay() {
    return composition({ .name = "camera25d_graph_2d_overlay", .width = 1280, .height = 720, .duration = 61 }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 t = static_cast<f32>(ctx.frame) / 60.0f;
        s.camera_2_5d({ .enabled = true, .position = {interpolate(t, 0.0f, 1.0f, -200.0f, 200.0f), 0, -1000}, .zoom = 1000.0f });

        s.layer("bg_3d", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, 0});
            l.rect("green", { .size = {1280, 720}, .color = Color{0, 0.3f, 0, 1}, .pos = {0, 0, 0} });
        });

        s.layer("ui_2d", [](LayerBuilder& l) {
            l.rect("bar", { .size = {600, 100}, .color = Color::white(), .pos = {0, -250, 0} });
            l.text("t", {
                .content = "2D UI OVERLAY",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 40.0f, .color = Color::black() },
                .pos = {-150, -265, 0}
            });
        });

        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("camera25d_graph_2d_overlay", camera25d_graph_2d_overlay)

// 6 & 7. Camera push-in test
static Composition camera25d_graph_pushin() {
    return composition({ .name = "camera25d_graph_pushin", .width = 1280, .height = 720, .duration = 91 }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 t = static_cast<f32>(ctx.frame) / 90.0f;
        s.camera_2_5d({ .enabled = true, .position = {0, 0, -1000}, .zoom = interpolate(t, 0.0f, 1.0f, 1000.0f, 1500.0f) });

        s.rect("bg", { .size = {1280, 720}, .color = Color{0.05f, 0.05f, 0.05f, 1}, .pos = {0, 0, 0} });

        auto card = [&](std::string n, f32 z, Color c, f32 y) {
            s.layer(n, [=](LayerBuilder& l) {
                l.enable_3d().position({0, y, z});
                l.rect("r", { .size = {200, 200}, .color = c, .pos = {0, 0, 0} });
            });
        };
        card("far", 1200.0f, Color::blue(), -250);
        card("sub", 0.0f, Color::green(), 0);
        card("near", -500.0f, Color::red(), 250);

        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("camera25d_graph_pushin", camera25d_graph_pushin)

// 8. Culling test: layer behind camera invisible
static Composition camera25d_graph_culling() {
    return composition({ .name = "camera25d_graph_culling", .width = 1280, .height = 720 }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d({ .enabled = true, .position = {0, 0, -1000}, .zoom = 1000.0f });

        s.rect("gray", { .size = {1280, 720}, .color = Color{0.2f, 0.2f, 0.2f, 1}, .pos = {0, 0, 0} });

        s.layer("behind", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, -1200}); // Behind camera (-1000)
            l.rect("magenta", { .size = {1000, 1000}, .color = Color{1.0f, 0.0f, 1.0f, 1.0f}, .pos = {0, 0, 0} });
        });

        s.layer("status", [](LayerBuilder& l) {
            l.text("passed", {
                .content = "CULLING TEST PASSED (no magenta visible)",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 32.0f, .color = Color{0.0f, 1.0f, 0.0f, 1.0f} },
                .pos = {-350, 0, 0}
            });
        });

        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("camera25d_graph_culling", camera25d_graph_culling)

// 9. DOF test
static Composition camera25d_graph_dof() {
    return composition({ .name = "camera25d_graph_dof", .width = 1280, .height = 720 }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        s.camera_2_5d({
            .enabled = true, .position = {0, 0, -1000}, .zoom = 1000.0f,
            .dof = { .enabled = true, .focus_z = 0.0f, .aperture = 0.025f, .max_blur = 25.0f }
        });

        s.rect("bg", { .size = {1280, 720}, .color = Color{0.02f, 0.02f, 0.05f, 1}, .pos = {0, 0, 0} });

        auto card = [&](std::string n, f32 z, Color c, f32 x) {
            s.layer(n, [=](LayerBuilder& l) {
                l.enable_3d().position({x, 0, z});
                l.rect("r", { .size = {250, 250}, .color = c, .pos = {0, 0, 0} });
                l.text("l", { .content = "Z=" + std::to_string((int)z), .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 30, .color = Color{1,1,1,1} }, .pos = {-30, 0, 0} });
            });
        };
        card("far", 1200.0f, Color{0, 0, 1, 1}, -400);
        card("sub", 0.0f, Color{0, 1, 0, 1}, 0);
        card("near", -500.0f, Color{1, 0, 0, 1}, 400);

        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("camera25d_graph_dof", camera25d_graph_dof)

// 10. Simple realistic scene
static Composition camera25d_graph_scena() {
    return composition({ .name = "camera25d_graph_scena", .width = 1280, .height = 720, .duration = 61 }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 t = static_cast<f32>(ctx.frame) / 60.0f;
        s.camera_2_5d({ .enabled = true, .position = {interpolate(t, 0.0f, 1.0f, -100.0f, 100.0f), 0, -1200}, .zoom = 1000.0f });

        s.rect("sky", { .size = {1280, 720}, .color = Color{0.1f, 0.15f, 0.3f, 1}, .pos = {0, 0, 0} });

        s.layer("montagne", [](LayerBuilder& l) {
            l.enable_3d().depth_role(DepthRole::FarBackground);
            l.rect("m1", { .size = {2000, 400}, .color = Color{0.05f, 0.08f, 0.15f, 1}, .pos = {0, 200, 0} });
        });

        s.layer("soggetto", [](LayerBuilder& l) {
            l.enable_3d().depth_role(DepthRole::Subject);
            l.rounded_rect("s", { .size = {400, 250}, .radius = 20, .color = Color{0.2f, 0.4f, 0.9f, 1}, .pos = {0, 0, 0} });
        });

        s.layer("cornice", [](LayerBuilder& l) {
            l.enable_3d().depth_role(DepthRole::Foreground).opacity(0.7f);
            l.rect("c1", { .size = {300, 1000}, .color = Color{0, 0, 0, 1}, .pos = {-600, 0, 0} });
            l.rect("c2", { .size = {300, 1000}, .color = Color{0, 0, 0, 1}, .pos = {600, 0, 0} });
        });

        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("camera25d_graph_scena", camera25d_graph_scena)

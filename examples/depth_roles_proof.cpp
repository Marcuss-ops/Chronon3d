#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

// Demonstrates all DepthRoles stacked in Camera 2.5D.
// Camera pans slowly in X so parallax between roles is visible.
static Composition DepthRolesProof() {
    return composition({
        .name = "DepthRolesProof", .width = 1280, .height = 720, .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const auto cam_x = interpolate(ctx.frame, 0, 90, -180.0f, 180.0f);
        s.camera_2_5d({
            .enabled          = true,
            .position         = {cam_x, 0, -1000},
            .point_of_interest = {0, 0, 0},
            .zoom             = 1000.0f
        });

        // ---- FarBackground ----
        s.layer("far-bg", [](LayerBuilder& l) {
            l.enable_3d(true)
             .position({640, 360, 0})
             .depth_role(DepthRole::FarBackground);
            l.image("checker", {
                .path = "assets/images/checker.png",
                .size = {1800, 1000}, .pos = {0, 0, 0}, .opacity = 0.25f
            });
        });

        // ---- Background ----
        s.layer("bg", [](LayerBuilder& l) {
            l.enable_3d(true)
             .position({640, 360, 0})
             .depth_role(DepthRole::Background);
            l.rect("r", {
                .size = {1280, 720}, .color = Color{0.08f, 0.10f, 0.18f, 1}, .pos = {0, 0, 0}
            });
            l.text("lbl", {
                .content = "BACKGROUND",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 24,
                           .color = Color{1,1,1,0.4f}, .align = TextAlign::Center }
            });
        });

        // ---- Midground ----
        s.layer("mid", [](LayerBuilder& l) {
            l.enable_3d(true)
             .position({640, 360, 0})
             .depth_role(DepthRole::Midground);
            l.rounded_rect("card", {
                .size = {600, 200}, .radius = 20,
                .color = Color{0.12f, 0.18f, 0.36f, 0.9f}, .pos = {0, 0, 0}
            });
            l.text("lbl", {
                .content = "MIDGROUND",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 30,
                           .color = Color{1,1,1,0.6f}, .align = TextAlign::Center }
            });
        });

        // ---- Subject ----
        s.layer("subject", [](LayerBuilder& l) {
            l.enable_3d(true)
             .position({640, 360, 0})
             .depth_role(DepthRole::Subject);
            l.rounded_rect("card", {
                .size = {520, 260}, .radius = 28,
                .color = Color{0.18f, 0.28f, 0.70f, 1}, .pos = {0, 0, 0}
            });
            l.text("lbl", {
                .content = "SUBJECT",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 52,
                           .color = Color{1,1,1,1}, .align = TextAlign::Center }
            });
        });

        // ---- Atmosphere (subtle layer closer than subject) ----
        s.layer("atmosphere", [](LayerBuilder& l) {
            l.enable_3d(true)
             .position({640, 360, 0})
             .depth_role(DepthRole::Atmosphere)
             .tint(Color{0.5f, 0.6f, 1.0f, 0.12f});
            l.rect("haze", {
                .size = {1400, 800}, .color = Color{0.3f, 0.4f, 0.9f, 0.06f}, .pos = {0, 0, 0}
            });
        });

        // ---- Foreground ----
        s.layer("fg", [](LayerBuilder& l) {
            l.enable_3d(true)
             .position({640, 580, 0})
             .depth_role(DepthRole::Foreground);
            l.text("lbl", {
                .content = "FOREGROUND",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 44,
                           .color = Color{1.0f, 0.82f, 0.28f, 1}, .align = TextAlign::Center }
            });
        });

        // ---- Overlay (2D HUD — no enable_3d, not affected by camera) ----
        s.layer("overlay", [](LayerBuilder& l) {
            l.position({640, 42, 0});  // depth_role not needed for 2D HUD
            l.text("hud", {
                .content = "OVERLAY (2D HUD — stays fixed)",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 22,
                           .color = Color{1,1,1,0.6f}, .align = TextAlign::Center }
            });
        });

        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("DepthRolesProof", DepthRolesProof)


// Shows depth_offset for fine-tuning within a role.
static Composition DepthRoleOffsetProof() {
    return composition({
        .name = "DepthRoleOffsetProof", .width = 1280, .height = 720, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera_2_5d({
            .enabled = true, .position = {0, 0, -1000}, .zoom = 1000.0f
        });
        s.rect("bg", { .size = {1280, 720}, .color = Color{0.03f, 0.03f, 0.05f, 1},
                       .pos = {640, 360, 0} });

        // Three layers all with Background role but different offsets
        for (int i = 0; i < 3; ++i) {
            const f32 x = 280.0f + i * 360.0f;
            const f32 off = (i - 1) * 300.0f; // -300, 0, +300
            s.layer("bg-" + std::to_string(i), [&](LayerBuilder& l) {
                l.enable_3d(true)
                 .position({x, 360, 0})
                 .depth_role(DepthRole::Background)
                 .depth_offset(off);
                l.rounded_rect("c", {
                    .size = {240, 160}, .radius = 16,
                    .color = Color{0.2f + i*0.15f, 0.35f, 0.75f, 1}, .pos = {0, 0, 0}
                });
                l.text("lbl", {
                    .content = (off > 0 ? "+300" : (off < 0 ? "-300" : "  0")),
                    .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 32,
                               .color = Color{1,1,1,1}, .align = TextAlign::Center }
                });
            });
        }

        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("DepthRoleOffsetProof", DepthRoleOffsetProof)

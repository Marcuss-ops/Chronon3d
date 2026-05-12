#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

// Demonstrates all DepthRoles stacked in Camera 2.5D.
// Camera pans slowly in X so parallax between roles is visible.
static Composition DepthRolesProof() {
    return composition({
        .name = "DepthRolesProof", .width = 1280, .height = 720, .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const auto cam_x = interpolate(ctx.frame, 0, 90, -300.0f, 300.0f);
        s.camera_2_5d({
            .enabled          = true,
            .position         = {cam_x, 0, -4000},
            .point_of_interest = {0, 0, 0},
            .zoom             = 4000.0f
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
             .position({640, 500, 0})
             .depth_role(DepthRole::Foreground);
            l.text("lbl", {
                .content = "FOREGROUND",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 44,
                           .color = Color{1.0f, 0.82f, 0.28f, 1}, .align = TextAlign::Center }
            });
        });

        // ---- Overlay (2D HUD - no enable_3d, not affected by camera) ----
        s.layer("overlay", [](LayerBuilder& l) {
            l.position({640, 42, 0});
            l.text("hud", {
                .content = "OVERLAY (2D HUD - stays fixed)",
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

// ---------------------------------------------------------------------------
// ZSortingProof
// Three overlapping cards at different Z depths.
// Near card (red, inserted FIRST) must render on top of far card (blue, inserted LAST).
// Camera at z=-4000 so scale differences are small (~5-15%) — cards stay readable.
// Insertion order: near → mid → far (backwards from expected 2D painter order).
// ---------------------------------------------------------------------------
static Composition ZSortingProof() {
    return composition({
        .name = "ZSortingProof", .width = 1280, .height = 720, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Camera far back so the depth-role Z values (-500, 0, +1200) produce
        // only subtle scale variation — avoids the near card swallowing everything.
        s.camera_2_5d({
            .enabled = true, .position = {0, 0, -4000}, .zoom = 4000.0f
        });
        s.rect("bg", { .size = {1280, 720}, .color = Color{0.03f, 0.03f, 0.05f, 1},
                        .pos = {640, 360, 0} });

        // Label: insertion order is intentionally reversed vs depth
        s.layer("title", [](LayerBuilder& l) {
            l.position({640, 60, 0});
            l.text("t", { .content = "Z-SORT: inserted near->mid->far  |  should render far->mid->near",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 22,
                           .color = Color{1,1,1,0.6f}, .align = TextAlign::Center } });
        });

        // Near-red — inserted FIRST, depth_role Foreground (z=-500).
        // After depth sort this must paint LAST (on top).
        s.layer("near-red", [](LayerBuilder& l) {
            l.enable_3d(true).position({700, 380, 0}).depth_role(DepthRole::Foreground);
            l.rounded_rect("r", { .size = {420, 260}, .radius = 24,
                .color = Color{0.85f, 0.22f, 0.22f, 0.95f}, .pos = {0, 0, 0} });
            l.text("l1", { .content = "NEAR  (Foreground, z=-500)",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 24,
                           .color = Color{1,1,1,1}, .align = TextAlign::Center },
                .pos = {0, -18, 0} });
            l.text("l2", { .content = "inserted 1st - renders on top",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 17,
                           .color = Color{1,1,1,0.7f}, .align = TextAlign::Center },
                .pos = {0, 18, 0} });
        });

        // Mid-green — inserted SECOND, Subject (z=0).
        s.layer("mid-green", [](LayerBuilder& l) {
            l.enable_3d(true).position({640, 360, 0}).depth_role(DepthRole::Subject);
            l.rounded_rect("r", { .size = {420, 260}, .radius = 24,
                .color = Color{0.18f, 0.70f, 0.28f, 0.95f}, .pos = {0, 0, 0} });
            l.text("l1", { .content = "MID  (Subject, z=0)",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 24,
                           .color = Color{1,1,1,1}, .align = TextAlign::Center },
                .pos = {0, -18, 0} });
            l.text("l2", { .content = "inserted 2nd - middle layer",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 17,
                           .color = Color{1,1,1,0.7f}, .align = TextAlign::Center },
                .pos = {0, 18, 0} });
        });

        // Far-blue — inserted LAST, Background (z=+1200).
        // Must paint FIRST (behind both), despite being inserted last.
        s.layer("far-blue", [](LayerBuilder& l) {
            l.enable_3d(true).position({580, 340, 0}).depth_role(DepthRole::Background);
            l.rounded_rect("r", { .size = {420, 260}, .radius = 24,
                .color = Color{0.22f, 0.33f, 0.90f, 0.95f}, .pos = {0, 0, 0} });
            l.text("l1", { .content = "FAR  (Background, z=+1200)",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 24,
                           .color = Color{1,1,1,1}, .align = TextAlign::Center },
                .pos = {0, -18, 0} });
            l.text("l2", { .content = "inserted last - renders behind",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 17,
                           .color = Color{1,1,1,0.7f}, .align = TextAlign::Center },
                .pos = {0, 18, 0} });
        });

        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("ZSortingProof", ZSortingProof)

// ---------------------------------------------------------------------------
// DepthOffsetProof
// Three Subject-role cards with offset -200 / 0 / +200.
// Card with negative offset appears larger (closer); +200 appears smaller (farther).
// ---------------------------------------------------------------------------
static Composition DepthOffsetProof() {
    return composition({
        .name = "DepthOffsetProof", .width = 1280, .height = 720, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera_2_5d({
            .enabled = true, .position = {0, 0, -1000}, .zoom = 1000.0f
        });
        s.rect("bg", { .size = {1280, 720}, .color = Color{0.03f, 0.03f, 0.05f, 1},
                        .pos = {640, 360, 0} });

        // 2D title HUD
        s.layer("title", [](LayerBuilder& l) {
            l.position({640, 55, 0});
            l.text("t", { .content = "depth_offset: same role, different Z fine-tuning -> different scale",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 22,
                           .color = Color{1,1,1,0.55f}, .align = TextAlign::Center } });
        });

        struct C { f32 x; f32 off; Color col; const char* line1; const char* line2; };
        for (auto [x, off, col, line1, line2] : std::initializer_list<C>{
            {280,   200, Color{0.25f, 0.55f, 0.9f,  1}, "offset +200", "(farther / smaller)"},
            {640,     0, Color{0.35f, 0.75f, 0.35f, 1}, "offset  0",   "(Subject baseline)"},
            {1000, -200, Color{0.9f,  0.35f, 0.25f, 1}, "offset -200", "(closer / larger)"},
        }) {
            s.layer("card", [&](LayerBuilder& l) {
                l.enable_3d(true)
                 .position({x, 360, 0})
                 .depth_role(DepthRole::Subject)
                 .depth_offset(off);
                l.rounded_rect("r", { .size = {280, 180}, .radius = 20,
                    .color = col, .pos = {0, 0, 0} });
                l.text("l1", { .content = line1,
                    .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 28,
                               .color = Color{1,1,1,1}, .align = TextAlign::Center },
                    .pos = {0, -20, 0} });
                l.text("l2", { .content = line2,
                    .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 18,
                               .color = Color{1,1,1,0.7f}, .align = TextAlign::Center },
                    .pos = {0, 20, 0} });
            });
        }

        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("DepthOffsetProof", DepthOffsetProof)

// ---------------------------------------------------------------------------
// ParallaxRolesProof
// Camera pans X while layers at different depth roles move at different rates.
// FarBackground moves least, Foreground moves most — visible parallax separation.
// ---------------------------------------------------------------------------
static Composition ParallaxRolesProof() {
    return composition({
        .name = "ParallaxRolesProof", .width = 1280, .height = 720, .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const auto cam_x = keyframes(ctx.frame, {
            KF{  0, -220.0f, Easing::InOutCubic },
            KF{ 60,  220.0f, Easing::InOutCubic },
            KF{120, -220.0f }
        });

        s.camera_2_5d({
            .enabled = true, .position = {cam_x, 0, -4000}, .zoom = 4000.0f
        });

        s.rect("bg-fill", { .size = {1280, 720},
            .color = Color{0.02f, 0.02f, 0.04f, 1}, .pos = {640, 360, 0} });

        // FarBackground — moves least
        s.layer("far-bg", [](LayerBuilder& l) {
            l.enable_3d(true).position({640, 360, 0})
             .depth_role(DepthRole::FarBackground);
            l.image("checker", { .path = "assets/images/checker.png",
                .size = {1800, 1000}, .pos = {0, 0, 0}, .opacity = 0.2f });
        });

        // Background
        s.layer("bg", [](LayerBuilder& l) {
            l.enable_3d(true).position({640, 360, 0}).depth_role(DepthRole::Background);
            l.rect("r", { .size = {1100, 580},
                .color = Color{0.08f, 0.10f, 0.20f, 0.7f}, .pos = {0, 0, 0} });
            l.text("lbl", { .content = "BACKGROUND",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 22,
                           .color = Color{1,1,1,0.35f}, .align = TextAlign::Center } });
        });

        // Midground
        s.layer("mid", [](LayerBuilder& l) {
            l.enable_3d(true).position({640, 360, 0}).depth_role(DepthRole::Midground);
            l.rounded_rect("r", { .size = {600, 260}, .radius = 24,
                .color = Color{0.12f, 0.20f, 0.45f, 0.85f}, .pos = {0, 0, 0} });
            l.text("lbl", { .content = "MIDGROUND",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 32,
                           .color = Color{1,1,1,0.55f}, .align = TextAlign::Center } });
        });

        // Subject
        s.layer("subject", [](LayerBuilder& l) {
            l.enable_3d(true).position({640, 360, 0}).depth_role(DepthRole::Subject);
            l.rounded_rect("r", { .size = {480, 240}, .radius = 28,
                .color = Color{0.18f, 0.32f, 0.75f, 1}, .pos = {0, 0, 0} });
            l.text("lbl", { .content = "SUBJECT",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 48,
                           .color = Color{1,1,1,1}, .align = TextAlign::Center } });
        });

        // Foreground — moves most
        s.layer("fg", [](LayerBuilder& l) {
            l.enable_3d(true).position({640, 500, 0}).depth_role(DepthRole::Foreground);
            l.text("lbl", { .content = "FOREGROUND",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 40,
                           .color = Color{1.0f, 0.85f, 0.3f, 1}, .align = TextAlign::Center } });
        });

        // 2D HUD - stays fixed regardless of camera
        s.layer("hud", [](LayerBuilder& l) {
            l.position({640, 40, 0});
            l.text("t", { .content = "2D HUD - FIXED",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 20,
                           .color = Color{1,1,1,0.5f}, .align = TextAlign::Center } });
        });

        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("ParallaxRolesProof", ParallaxRolesProof)

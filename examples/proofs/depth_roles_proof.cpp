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
// Three cards in a cascade layout. Insertion order is NEAR first, FAR last —
// the opposite of correct render order. Validates depth sort.
// Each card is staggered so all three are readable even with overlap.
// ---------------------------------------------------------------------------
static Composition ZSortingProof() {
    return composition({
        .name = "ZSortingProof", .width = 1280, .height = 720, .duration = 1
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.camera_2_5d({
            .enabled = true, .position = {0, 0, -4000}, .zoom = 4000.0f
        });
        s.rect("bg", { .size = {1280, 720}, .color = Color{0.03f, 0.03f, 0.05f, 1},
                        .pos = {640, 360, 0} });

        s.layer("title", [](LayerBuilder& l) {
            l.position({640, 48, 0});
            l.text("t1", { .content = "Z-SORT PROOF",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 30,
                           .color = Color{1,1,1,0.9f}, .align = TextAlign::Center } });
            l.text("t2", { .content = "Insertion order: NEAR first, FAR last  |  correct render: FAR behind, NEAR on top",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 17,
                           .color = Color{1,1,1,0.45f}, .align = TextAlign::Center },
                .pos = {0, 36, 0} });
        });

        // Cascade: top-left(far) -> center(mid) -> bottom-right(near)
        // FAR-blue inserted LAST — must paint first (behind)
        s.layer("far", [](LayerBuilder& l) {
            l.enable_3d(true).position({480, 310, 0}).depth_role(DepthRole::Background);
            l.rounded_rect("r", { .size = {520, 175}, .radius = 20,
                .color = Color{0.20f, 0.28f, 0.88f, 1}, .pos = {0, 0, 0} });
            l.text("l1", { .content = "FAR   z=+1200   Background",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 26,
                           .color = Color{1,1,1,1}, .align = TextAlign::Center },
                .pos = {0, -14, 0} });
            l.text("l2", { .content = "inserted LAST -> renders BEHIND",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 17,
                           .color = Color{1,1,1,0.65f}, .align = TextAlign::Center },
                .pos = {0, 20, 0} });
        });

        // MID-green inserted SECOND
        s.layer("mid", [](LayerBuilder& l) {
            l.enable_3d(true).position({620, 385, 0}).depth_role(DepthRole::Subject);
            l.rounded_rect("r", { .size = {520, 175}, .radius = 20,
                .color = Color{0.14f, 0.64f, 0.22f, 1}, .pos = {0, 0, 0} });
            l.text("l1", { .content = "MID   z=0   Subject",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 26,
                           .color = Color{1,1,1,1}, .align = TextAlign::Center },
                .pos = {0, -14, 0} });
            l.text("l2", { .content = "inserted 2nd -> middle layer",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 17,
                           .color = Color{1,1,1,0.65f}, .align = TextAlign::Center },
                .pos = {0, 20, 0} });
        });

        // NEAR-red inserted FIRST — must paint last (on top)
        s.layer("near", [](LayerBuilder& l) {
            l.enable_3d(true).position({760, 460, 0}).depth_role(DepthRole::Foreground);
            l.rounded_rect("r", { .size = {520, 175}, .radius = 20,
                .color = Color{0.88f, 0.18f, 0.18f, 1}, .pos = {0, 0, 0} });
            l.text("l1", { .content = "NEAR   z=-500   Foreground",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 26,
                           .color = Color{1,1,1,1}, .align = TextAlign::Center },
                .pos = {0, -14, 0} });
            l.text("l2", { .content = "inserted FIRST -> renders ON TOP",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 17,
                           .color = Color{1,1,1,0.65f}, .align = TextAlign::Center },
                .pos = {0, 20, 0} });
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
// Each depth role gets its own card at a unique X position.
// Camera pans X: cards at different depths drift at different rates.
// FarBackground drifts least, Foreground drifts most.
// Fixed 2D legend at bottom lists every role with its Z value.
// ---------------------------------------------------------------------------
static Composition ParallaxRolesProof() {
    return composition({
        .name = "ParallaxRolesProof", .width = 1280, .height = 720, .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const auto cam_x = keyframes(ctx.frame, {
            KF{  0, -260.0f, Easing::InOutCubic },
            KF{ 60,  260.0f, Easing::InOutCubic },
            KF{120, -260.0f }
        });

        s.camera_2_5d({
            .enabled = true, .position = {cam_x, 0, -4000}, .zoom = 4000.0f
        });

        s.rect("bg-fill", { .size = {1280, 720},
            .color = Color{0.02f, 0.02f, 0.04f, 1}, .pos = {640, 360, 0} });

        // Full-canvas checker so FarBackground movement is clear
        s.layer("far-bg", [](LayerBuilder& l) {
            l.enable_3d(true).position({640, 360, 0}).depth_role(DepthRole::FarBackground);
            l.image("checker", { .path = "assets/images/checker.png",
                .size = {2200, 720}, .pos = {0, 0, 0}, .opacity = 0.18f });
        });

        // Five role cards spread across X so none overlap.
        // World positions are intentionally wide — the camera pan reveals
        // that each card drifts a different number of pixels per frame.
        struct RoleCard {
            const char* id;
            f32 wx;      // world X
            DepthRole role;
            Color col;
            const char* label;
            const char* sub;
        };
        for (auto [id, wx, role, col, label, sub] : std::initializer_list<RoleCard>{
            {"bg",   160, DepthRole::Background,    Color{0.18f,0.25f,0.70f,0.9f}, "BACKGROUND",  "z=+1200"},
            {"mg",   370, DepthRole::Midground,     Color{0.18f,0.48f,0.70f,0.9f}, "MIDGROUND",   "z=+600"},
            {"sub",  640, DepthRole::Subject,       Color{0.22f,0.35f,0.85f,1.0f}, "SUBJECT",     "z=0"},
            {"atm",  900, DepthRole::Atmosphere,    Color{0.32f,0.48f,0.75f,0.9f}, "ATMOSPHERE",  "z=-300"},
            {"fg",  1120, DepthRole::Foreground,    Color{0.65f,0.48f,0.12f,1.0f}, "FOREGROUND",  "z=-500"},
        }) {
            s.layer(id, [&](LayerBuilder& l) {
                l.enable_3d(true).position({wx, 360, 0}).depth_role(role);
                l.rounded_rect("r", { .size = {200, 150}, .radius = 16,
                    .color = col, .pos = {0, 0, 0} });
                l.text("l1", { .content = label,
                    .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 20,
                               .color = Color{1,1,1,1}, .align = TextAlign::Center },
                    .pos = {0, -14, 0} });
                l.text("l2", { .content = sub,
                    .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 16,
                               .color = Color{1,1,1,0.65f}, .align = TextAlign::Center },
                    .pos = {0, 16, 0} });
            });
        }

        // Fixed 2D HUD: title and legend — never moves
        s.layer("hud-title", [](LayerBuilder& l) {
            l.position({640, 36, 0});
            l.text("t", { .content = "PARALLAX PROOF  |  camera pans X, each role drifts at its own rate",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 19,
                           .color = Color{1,1,1,0.55f}, .align = TextAlign::Center } });
        });

        s.layer("hud-legend", [](LayerBuilder& l) {
            l.position({640, 690, 0});
            l.text("t", { .content = "FarBG z=+2000  |  BG z=+1200  |  MG z=+600  |  Subject z=0  |  Atm z=-300  |  FG z=-500  |  HUD 2D fixed",
                .style = { .font_path = "assets/fonts/Inter-Bold.ttf", .size = 15,
                           .color = Color{1,1,1,0.40f}, .align = TextAlign::Center } });
        });

        return s.build();
    });
}
CHRONON_REGISTER_COMPOSITION("ParallaxRolesProof", ParallaxRolesProof)

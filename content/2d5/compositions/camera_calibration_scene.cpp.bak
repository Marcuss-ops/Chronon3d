#include "camera_calibration_scene.hpp"
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <cmath>

namespace chronon3d::content::two_point_five_d {

// ── Constants ─────────────────────────────────────────────────────────────
constexpr Color kBgColor{0.04f, 0.04f, 0.06f, 1.0f};

// ── Asymmetric calibration card dimensions ────────────────────────────────
// Card is sized to ~1.44x the previous 500×350 to fill the canvas better
// when framed at 1920×1080 (≈37% width vs former ≈26% width).
constexpr f32 kCardW = 720.0f;
constexpr f32 kCardH = 504.0f;
constexpr f32 kCardHalfW = kCardW * 0.5f;
constexpr f32 kCardHalfH = kCardH * 0.5f;

// ── Helper: corner marker rect (small colored square at a card corner) ────
static void add_corner_marker(LayerBuilder& l, const char* name,
                              f32 x, f32 y, Color color) {
    l.rect(name, {
        .size = {32.0f, 32.0f},
        .color = color,
        .pos = {x, y, 0.1f}
    });
}

void add_camera_calibration_scene(SceneBuilder& s, bool include_pillars) {
    // ── 1. Ambient & directional lights ─────────────────────────────────
    s.ambient_light({1.0f, 1.0f, 1.0f, 1.0f}, 0.45f);
    s.directional_light({0.0f, 0.0f, -1.0f},
                        {1.0f, 1.0f, 1.0f, 1.0f}, 0.55f);

    // ── 2. Full-screen background fill (dark gray) ──────────────────────
    s.layer("bg_fill", [](LayerBuilder& l) {
        l.pin_to(Anchor::Center);
        l.rect("bg", {
            .size = {1920.0f, 1080.0f},
            .color = kBgColor,
            .pos = {0.0f, 0.0f, -10.0f}
        });
    });

    // ── 3. Ground grid plane (XZ at Y=+250) ─────────────────────────────
    // Y-down convention: positive Y = down = floor, negative Y = up = ceiling
    // So Y=+250 places the grid BELOW the origin, as a floor, not a ceiling.
    s.layer("floor_grid", [](LayerBuilder& l) {
        l.cache_static().enable_3d().position({0.0f, 250.0f, 0.0f});
        l.grid_plane("grid", {
            .pos = {0.0f, 0.0f, 0.0f},
            .axis = PlaneAxis::XZ,
            .extent = 2500.0f,
            .spacing = 120.0f,
            .color = {0.15f, 0.30f, 0.70f, 0.20f},
            .fade_distance = 2000.0f,
            .fade_min_alpha = 0.02f
        });
    });

    // ── 4. Camera target null (always at origin) ────────────────────────
    s.null_layer("camera_target", [](NullBuilder& n) {
        n.position({0.0f, 0.0f, 0.0f});
    });

    // ── 5. Camera parent null (for parent-rig tests) ────────────────────
    s.null_layer("camera_parent", [](NullBuilder& n) {
        n.position({0.0f, 0.0f, 0.0f});
    });

    // ── 6. World-space axes (RGB) ───────────────────────────────────────
    s.layer("axis_x", [](LayerBuilder& l) {
        l.enable_3d().position({0.0f, 0.0f, 0.0f});
        l.line("x_line", {
            .from = {0.0f, 0.0f, 0.0f},
            .to = {200.0f, 0.0f, 0.0f},
            .thickness = 3.0f,
            .color = {1.0f, 0.2f, 0.2f, 0.85f}
        });
        l.text("x_lbl", {
            .text = "X",
            .pos = {208.0f, 0.0f, 0.1f},
            .font_size = 18.0f,
            .color = {1.0f, 0.2f, 0.2f, 0.85f},
            .anchor = TextAnchor::TopLeft,
            .align = TextAlign::Left
        });
    });

    s.layer("axis_y", [](LayerBuilder& l) {
        l.enable_3d().position({0.0f, 0.0f, 0.0f});
        l.line("y_line", {
            .from = {0.0f, 0.0f, 0.0f},
            .to = {0.0f, 200.0f, 0.0f},
            .thickness = 3.0f,
            .color = {0.2f, 1.0f, 0.2f, 0.85f}
        });
        l.text("y_lbl", {
            .text = "Y",
            .pos = {0.0f, 208.0f, 0.1f},
            .font_size = 18.0f,
            .color = {0.2f, 1.0f, 0.2f, 0.85f},
            .anchor = TextAnchor::BottomCenter,
            .align = TextAlign::Center
        });
    });

    s.layer("axis_z", [](LayerBuilder& l) {
        l.enable_3d().position({0.0f, 0.0f, 0.0f});
        l.line("z_line", {
            .from = {0.0f, 0.0f, 0.0f},
            .to = {0.0f, 0.0f, -200.0f},
            .thickness = 3.0f,
            .color = {0.2f, 0.2f, 1.0f, 0.85f}
        });
        l.text("z_lbl", {
            .text = "Z",
            .pos = {0.0f, 0.0f, -210.0f},
            .font_size = 18.0f,
            .color = {0.2f, 0.2f, 1.0f, 0.85f},
            .anchor = TextAnchor::Center,
            .align = TextAlign::Center
        });
    });

    // ── 7. World-space target cross at origin ───────────────────────────
    s.layer("target_cross", [](LayerBuilder& l) {
        l.enable_3d().position({0.0f, 0.0f, 0.0f});
        l.line("cross_hl", {
            .from = {-25.0f, 0.0f, 0.2f},
            .to = {25.0f, 0.0f, 0.2f},
            .thickness = 2.5f,
            .color = {1.0f, 0.35f, 0.35f, 0.9f}
        });
        l.line("cross_vl", {
            .from = {0.0f, -25.0f, 0.2f},
            .to = {0.0f, 25.0f, 0.2f},
            .thickness = 2.5f,
            .color = {1.0f, 0.35f, 0.35f, 0.9f}
        });
        l.circle("target_ring", {
            .radius = 16.0f,
            .color = {1.0f, 0.35f, 0.35f, 0.0f},
            .pos = {0.0f, 0.0f, 0.15f},
            .stroke = {.enabled = true, .color = {1.0f, 0.35f, 0.35f, 0.5f}, .width = 2.0f}
        });
        l.text("lbl", {
            .text = "TARGET",
            .pos = {30.0f, -6.0f, 0.2f},
            .font_size = 11.0f,
            .color = {1.0f, 0.45f, 0.45f, 0.75f},
            .anchor = TextAnchor::TopLeft,
            .align = TextAlign::Left
        });
    });

    // ── 8. Three pillars at different Z depths ──────────────────────────
    // Near pillar (Z=-400) — closest, tallest, cyan
    s.layer("pillar_near", [](LayerBuilder& l) {
        l.cache_static().enable_3d().position({400.0f, 0.0f, -400.0f})
         .casts_shadows(true).accepts_shadows(true);
        l.fake_box3d("box", {
            .size = {60.0f, 380.0f},
            .depth = 60.0f,
            .color = {0.04f, 0.45f, 0.72f, 1.0f},
            .top_tint = 0.20f,
            .side_tint = 0.25f,
            .contact_shadow = true,
            .floor_y = -250.0f
        });
    });

    // Mid pillar (Z=0) — medium, blue, at left
    s.layer("pillar_mid", [](LayerBuilder& l) {
        l.cache_static().enable_3d().position({-350.0f, 0.0f, 0.0f})
         .casts_shadows(true).accepts_shadows(true);
        l.fake_box3d("box", {
            .size = {60.0f, 300.0f},
            .depth = 60.0f,
            .color = {0.18f, 0.30f, 0.65f, 1.0f},
            .top_tint = 0.15f,
            .side_tint = 0.20f,
            .contact_shadow = true,
            .floor_y = -250.0f
        });
    });

    // Far pillar (Z=+400) — farthest, shortest, dark muted
    s.layer("pillar_far", [](LayerBuilder& l) {
        l.cache_static().enable_3d().position({300.0f, 0.0f, 400.0f})
         .casts_shadows(true).accepts_shadows(true);
        l.fake_box3d("box", {
            .size = {50.0f, 220.0f},
            .depth = 50.0f,
            .color = {0.06f, 0.09f, 0.22f, 1.0f},
            .top_tint = 0.18f,
            .side_tint = 0.22f,
            .contact_shadow = true,
            .floor_y = -250.0f
        });
    });

    // ── 9. Asymmetric calibration card at Z=0 ───────────────────────────
    s.layer("calibration_card", [](LayerBuilder& l) {
        l.cache_static().enable_3d().position({0.0f, 0.0f, 0.0f});

        // Card body (dark gray with 2-layer cyan border for clean aesthetic:
        // sharp 2px foreground + soft 8px aura behind).
        // Aura layer (drawn first, lower z, wider stroke, low alpha).
        l.rounded_rect("card_aura", {
            .size = {kCardW, kCardH},
            .radius = 14.0f,
            .color = {0.08f, 0.10f, 0.18f, 1.0f},
            .pos = {0.0f, 0.0f, -0.05f},
            .stroke = {
                .enabled = true,
                .color = {0.0f, 0.85f, 1.0f, 0.18f},
                .width = 8.0f
            }
        });
        // Sharp body + crisp 2px border on top.
        l.rounded_rect("card_body", {
            .size = {kCardW, kCardH},
            .radius = 12.0f,
            .color = {0.08f, 0.10f, 0.18f, 1.0f},
            .pos = {0.0f, 0.0f, 0.0f},
            .stroke = {
                .enabled = true,
                .color = {0.0f, 0.85f, 1.0f, 0.85f},
                .width = 2.0f
            }
        });

        // Colored corner markers (asymmetric: TL=red, TR=green, BR=blue)
        add_corner_marker(l, "corner_tl", -kCardHalfW + 4.0f, -kCardHalfH + 4.0f,
                          {1.0f, 0.15f, 0.15f, 0.95f});   // RED top-left
        add_corner_marker(l, "corner_tr",  kCardHalfW - 36.0f, -kCardHalfH + 4.0f,
                          {0.15f, 1.0f, 0.15f, 0.95f});   // GREEN top-right
        add_corner_marker(l, "corner_br",  kCardHalfW - 36.0f,  kCardHalfH - 36.0f,
                          {0.15f, 0.25f, 1.0f, 0.95f});   // BLUE bottom-right
        // Bottom-left intentionally left uncolored (asymmetry)

        // Yellow center dot
        l.circle("center_dot", {
            .radius = 6.0f,
            .color = {1.0f, 0.85f, 0.1f, 0.9f},
            .pos = {0.0f, 0.0f, 0.15f}
        });

        // Text labels — Y-down convention (positive Y = down on screen).
        // TOP is at NEGATIVE Y (above center), BOTTOM at POSITIVE Y (below center).
        l.text("label_top", {
            .text = "TOP",
            .pos = {0.0f, -kCardHalfH + 18.0f, 0.15f},
            .font_size = 20.0f,
            .color = {0.75f, 0.85f, 1.0f, 0.85f},
            .anchor = TextAnchor::Center,
            .align = TextAlign::Center
        });

        l.text("label_left", {
            .text = "LEFT",
            .pos = {-kCardHalfW + 20.0f, 0.0f, 0.15f},
            .font_size = 18.0f,
            .color = {0.75f, 0.85f, 1.0f, 0.75f},
            .anchor = TextAnchor::Center,
            .align = TextAlign::Center
        });

        l.text("label_front", {
            .text = "FRONT →",
            .pos = {0.0f, -4.0f, 0.15f},
            .font_size = 28.0f,
            .color = {0.90f, 0.95f, 1.0f, 1.0f},
            .anchor = TextAnchor::Center,
            .align = TextAlign::Center
        });

        l.text("label_right", {
            .text = "RIGHT",
            .pos = {kCardHalfW - 22.0f, 0.0f, 0.15f},
            .font_size = 18.0f,
            .color = {0.75f, 0.85f, 1.0f, 0.75f},
            .anchor = TextAnchor::Center,
            .align = TextAlign::Center
        });

        l.text("label_bottom", {
            .text = "BOTTOM",
            .pos = {0.0f, kCardHalfH - 18.0f, 0.15f},
            .font_size = 20.0f,
            .color = {0.75f, 0.85f, 1.0f, 0.85f},
            .anchor = TextAnchor::Center,
            .align = TextAlign::Center
        });
    });
}

std::vector<std::string> calibration_landmark_layers() {
    return {
        "calibration_card",
        "pillar_near",
        "pillar_mid",
        "pillar_far"
    };
}

} // namespace chronon3d::content::two_point_five_d

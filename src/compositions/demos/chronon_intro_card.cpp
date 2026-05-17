// ChrononIntroCard
//
// Animated showcase of Chronon3D 2.5D engine capabilities:
//   - "CHRONON 3D" fake-extruded title flies in from left (2D, immune to DOF)
//   - Camera pan + dolly reveals background/foreground parallax depth
//   - DOF: bg (z=600) blurs, glass card (z=0) stays sharp
//   - Feature pills appear staggered: PARALLAX, DEPTH OF FIELD, 2.5D CAMERA
//   - Glow sweep accent traverses the frame
//
// Duration: 180 frames (6 seconds at 30 fps)
// Export: chronon3d_cli video --comp ChrononIntroCard --end 179 --output output/chronon_intro.mp4

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <algorithm>
#include <cmath>
#include <string>

using namespace chronon3d;

static Composition chronon_intro_card() {
    return composition({
        .name     = "ChrononIntroCard",
        .width    = 1280,
        .height   = 720,
        .duration = 180
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const float t = ctx.duration > 1
            ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1)
            : 0.0f;

        // Smoothstep ramp clamped to [ta, tb] window
        auto ease = [](float a, float b, float ta, float tb, float tt) -> float {
            const float lt = std::clamp((tt - ta) / std::max(tb - ta, 1e-6f), 0.0f, 1.0f);
            const float st = lt * lt * (3.0f - 2.0f * lt);
            return a + (b - a) * st;
        };

        // ── Camera: dolly-in + pan revealing background parallax ──────────
        const float cam_x = ease(-28.0f, 28.0f, 0.0f, 1.0f, t);
        const float cam_z = ease(-1080.0f, -960.0f, 0.0f, 0.70f, t);

        s.camera().set({
            .enabled  = true,
            .position = {cam_x, 0.0f, cam_z},
            .zoom     = 1000.0f,
            .dof      = {.enabled=true, .focus_z=0.0f, .aperture=0.013f, .max_blur=11.0f}
        });

        // ── Background (z=600 → DOF blurred + parallax offset from cam pan) ─
        s.layer("bg", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 600.0f});
            l.rect("base", {
                .size  = {2800.0f, 1600.0f},
                .color = Color{0.04f, 0.04f, 0.09f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
            l.circle("orb_l", {
                .radius = 280.0f,
                .color  = Color{0.07f, 0.05f, 0.32f, 0.55f},
                .pos    = {-440.0f, -70.0f, 0.0f}
            }).blur(75.0f);
            l.circle("orb_r", {
                .radius = 210.0f,
                .color  = Color{0.22f, 0.04f, 0.26f, 0.45f},
                .pos    = {480.0f,  90.0f, 0.0f}
            }).blur(60.0f);
            l.grid_plane("grid", {
                .pos           = {0.0f, 220.0f, 0.0f},
                .axis          = PlaneAxis::XZ,
                .extent        = 2000.0f,
                .spacing       = 160.0f,
                .color         = Color{0.28f, 0.38f, 0.90f, 0.17f},
                .fade_distance = 1600.0f,
                .fade_min_alpha = 0.0f,
            });
        });

        // ── Glass card (z=0, in focus — sharp) ───────────────────────────
        s.layer("card", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 0.0f});
            l.rounded_rect("panel", {
                .size   = {660.0f, 380.0f},
                .radius = 18.0f,
                .color  = Color{0.045f, 0.045f, 0.11f, 0.90f},
                .pos    = {0.0f, 0.0f, 0.0f}
            });
            l.rect("accent_t", {
                .size  = {560.0f, 3.0f},
                .color = Color{0.42f, 0.64f, 1.00f, 1.0f},
                .pos   = {0.0f, -171.0f, 0.0f}
            });
            l.rect("accent_b", {
                .size  = {560.0f, 2.0f},
                .color = Color{0.32f, 0.22f, 0.72f, 0.55f},
                .pos   = {0.0f,  171.0f, 0.0f}
            });
        });

        // ── Title (2D, fly in from left) ──────────────────────────────────
        // No enable_3d → immune to DOF; fake_extruded_text gives the 3D look
        const float title_x = ease(-880.0f,  0.0f, 0.0f, 0.26f, t);
        const float title_a = ease(  0.0f,   1.0f, 0.0f, 0.18f, t);

        s.layer("title", [title_x, title_a](LayerBuilder& l) {
            l.position({title_x, -52.0f, 0.0f}).opacity(title_a);
            l.fake_extruded_text("main", {
                .text        = "CHRONON 3D",
                .font_path   = "assets/fonts/Inter-Bold.ttf",
                .pos         = {0.0f, 0.0f, 0.0f},
                .font_size   = 88.0f,
                .depth       = 16,
                .extrude_dir = {0.55f, 0.75f},
                .front_color = Color{1.00f, 1.00f, 1.00f, 1.0f},
                .side_color  = Color{0.38f, 0.52f, 0.96f, 0.82f},
            });
        });

        // ── Tagline (2D, slide up from below) ─────────────────────────────
        const float tag_y = ease(95.0f, 44.0f, 0.22f, 0.46f, t);
        const float tag_a = ease( 0.0f,  1.0f, 0.22f, 0.40f, t);

        s.layer("tagline", [tag_y, tag_a](LayerBuilder& l) {
            l.position({0.0f, tag_y, 0.0f}).opacity(tag_a);
            l.text("tl", {
                .content = "2.5D Motion Graphics Engine",
                .style = {
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size      = 29.0f,
                    .color     = Color{0.62f, 0.76f, 1.00f, 1.0f},
                    .align     = TextAlign::Center,
                },
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });

        // ── Divider rule ──────────────────────────────────────────────────
        const float div_a = ease(0.0f, 1.0f, 0.40f, 0.54f, t);

        s.layer("divider", [div_a](LayerBuilder& l) {
            l.position({0.0f, 72.0f, 0.0f}).opacity(div_a);
            l.rect("r", {
                .size  = {380.0f, 1.0f},
                .color = Color{0.42f, 0.52f, 0.90f, 0.40f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        // ── Feature pills (2D, staggered) ─────────────────────────────────
        struct PillDef { const char* label; float start_t; float cx; Color col; };
        static const PillDef pills[] = {
            {"PARALLAX",      0.46f, -222.0f, Color{0.28f, 0.58f, 1.00f, 1.0f}},
            {"DEPTH OF FIELD",0.55f,   0.0f,  Color{0.64f, 0.38f, 1.00f, 1.0f}},
            {"2.5D CAMERA",   0.64f,  222.0f, Color{0.32f, 0.88f, 0.56f, 1.0f}},
        };

        for (int pi = 0; pi < 3; ++pi) {
            const PillDef& pd = pills[pi];
            const float py  = ease(118.0f, 92.0f, pd.start_t, pd.start_t + 0.18f, t);
            const float pa  = ease(  0.0f,  1.0f, pd.start_t, pd.start_t + 0.14f, t);
            const Color bg_col{pd.col.r * 0.16f, pd.col.g * 0.16f, pd.col.b * 0.18f, 0.90f};
            const char* label = pd.label;
            const float cx    = pd.cx;
            const Color col   = pd.col;

            s.layer("pill" + std::to_string(pi), [py, pa, cx, col, bg_col, label](LayerBuilder& l) {
                l.position({cx, py, 0.0f}).opacity(pa);
                l.rounded_rect("bg", {
                    .size   = {188.0f, 40.0f},
                    .radius = 20.0f,
                    .color  = bg_col,
                    .pos    = {0.0f, 0.0f, 0.0f}
                });
                l.rect("top_border", {
                    .size  = {188.0f, 1.5f},
                    .color = col,
                    .pos   = {0.0f, -19.25f, 0.0f}
                });
                l.text("lbl", {
                    .content = label,
                    .style = {
                        .font_path = "assets/fonts/Inter-Bold.ttf",
                        .size      = 14.5f,
                        .color     = col,
                        .align     = TextAlign::Center,
                    },
                    .pos = {0.0f, 1.0f, 0.0f}
                });
            });
        }

        // ── Glow sweep accent (2D) ─────────────────────────────────────────
        const float sweep_x = ease(-720.0f, 720.0f, 0.04f, 0.56f, t);
        const float sweep_progress = std::clamp((t - 0.04f) / 0.52f, 0.0f, 1.0f);
        const float sweep_a = std::sin(sweep_progress * 3.14159f) * 0.38f;

        s.layer("sweep", [sweep_x, sweep_a](LayerBuilder& l) {
            l.position({sweep_x, -52.0f, 0.0f}).opacity(sweep_a);
            l.circle("g", {
                .radius = 150.0f,
                .color  = Color{0.32f, 0.52f, 1.00f, 0.22f},
                .pos    = {0.0f, 0.0f, 0.0f}
            }).blur(75.0f);
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ChrononIntroCard", chronon_intro_card)

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>

// Proof composition: all 9 object types in a single 2.5D scene.
// Render with --graph flag (modular graph required for Camera2_5D).
//
//   chronon3d_cli render Proof25DObjects --graph --frames 0   -o output/debug/25d/proof_000.png
//   chronon3d_cli render Proof25DObjects --graph --frames 45  -o output/debug/25d/proof_045.png
//   chronon3d_cli render Proof25DObjects --graph --frames 89  -o output/debug/25d/proof_089.png

using namespace chronon3d;

static Composition proof_25d_objects() {
    return composition({
        .name     = "Proof25DObjects",
        .width    = 1280,
        .height   = 720,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const float t = ctx.duration > 1
            ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1)
            : 0.0f;

        // Orbit around the scene with a slight dolly-in.
        s.camera().set(camera_motion::orbit(t, {
            .radius          = 220.0f,
            .z               = -1000.0f,
            .y               = -30.0f,
            .target          = {0.0f, 0.0f, 0.0f},
            .start_angle_deg = -18.0f,
            .end_angle_deg   =  18.0f,
            .zoom            = 1000.0f
        }));

        // ── 1. Background card (far) ──────────────────────────────────────────
        s.layer("background_card", [](LayerBuilder& l) {
            l.enable_3d()
             .position({0.0f, 0.0f, 1400.0f});
            l.rect("bg", {
                .size  = {2200.0f, 1300.0f},
                .color = Color{0.03f, 0.04f, 0.08f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        // ── 2. Grid plane (XZ) ───────────────────────────────────────────────
        s.layer("grid", [](LayerBuilder& l) {
            l.enable_3d()
             .position({0.0f, 200.0f, 600.0f});
            l.grid_plane("grid", {
                .pos           = {0.0f, 0.0f, 0.0f},
                .axis          = PlaneAxis::XZ,
                .extent        = 1800.0f,
                .spacing       = 80.0f,
                .color         = Color{0.15f, 0.35f, 1.0f, 0.40f},
                .fade_distance = 2400.0f,
                .fade_min_alpha = 0.04f
            });
        });

        // ── 3. Rect card (upper-left) ─────────────────────────────────────────
        s.layer("rect_card", [](LayerBuilder& l) {
            l.enable_3d()
             .position({-380.0f, -110.0f, 100.0f})
             .rotate({18.0f, -22.0f, 0.0f});
            l.rect("rect", {
                .size  = {280.0f, 170.0f},
                .color = Color{1.0f, 0.35f, 0.08f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        // ── 4. Image card (upper-center) ──────────────────────────────────────
        s.layer("image_card", [](LayerBuilder& l) {
            l.enable_3d()
             .position({0.0f, -130.0f, 0.0f})
             .rotate({14.0f, 18.0f, 0.0f});
            l.image("img", {
                .path    = "assets/images/camera_reference.jpg",
                .size    = {320.0f, 200.0f},
                .pos     = {0.0f, 0.0f, 0.0f},
                .opacity = 1.0f
            });
        });

        // ── 5. Text card (upper-right) ────────────────────────────────────────
        s.layer("text_card", [](LayerBuilder& l) {
            l.enable_3d()
             .position({370.0f, -110.0f, -160.0f})
             .rotate({20.0f, -18.0f, 0.0f});
            l.text("title", {
                .content = "2.5D",
                .style = {
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size      = 90.0f,
                    .color     = Color{1.0f, 1.0f, 1.0f, 1.0f},
                    .align     = TextAlign::Center
                },
                .pos = {0.0f, 0.0f, 0.0f}
            }).with_glow(Glow{
                .enabled   = true,
                .radius    = 14.0f,
                .intensity = 0.85f,
                .color     = Color{0.1f, 0.45f, 1.0f, 1.0f}
            });
        });

        // ── 6. Rounded rect (lower-left) ──────────────────────────────────────
        s.layer("rounded_rect_card", [](LayerBuilder& l) {
            l.enable_3d()
             .position({-390.0f, 140.0f, -80.0f})
             .rotate({-10.0f, 24.0f, 0.0f});
            l.rounded_rect("rounded", {
                .size   = {240.0f, 120.0f},
                .radius = 24.0f,
                .color  = Color{0.1f, 0.7f, 1.0f, 1.0f},
                .pos    = {0.0f, 0.0f, 0.0f}
            });
        });

        // ── 7. Circle (lower-center-left) ─────────────────────────────────────
        s.layer("circle_card", [](LayerBuilder& l) {
            l.enable_3d()
             .position({-130.0f, 140.0f, -260.0f})
             .rotate({0.0f, -28.0f, 0.0f});
            l.circle("circle", {
                .radius = 65.0f,
                .color  = Color{0.9f, 0.1f, 0.8f, 1.0f},
                .pos    = {0.0f, 0.0f, 0.0f}
            });
        });

        // ── 8. Fake box (lower-center-right) ──────────────────────────────────
        s.layer("fake_box", [](LayerBuilder& l) {
            l.enable_3d()
             .position({130.0f, 150.0f, -140.0f})
             .rotate({-10.0f, -22.0f, 0.0f});
            l.fake_box3d("box", {
                .pos   = {0.0f, 0.0f, 0.0f},
                .size  = {150.0f, 110.0f},
                .depth = 80.0f,
                .color = Color{0.95f, 0.55f, 0.08f, 1.0f}
            });
        });

        // ── 9. Line (diagonal, lower-right area) ──────────────────────────────
        s.layer("line_card", [](LayerBuilder& l) {
            l.enable_3d()
             .position({350.0f, 130.0f, -80.0f})
             .rotate({8.0f, -15.0f, 0.0f});
            l.line("diag", {
                .from      = {-80.0f, -60.0f, 0.0f},
                .to        = { 80.0f,  60.0f, 0.0f},
                .thickness = 3.0f,
                .color     = Color{1.0f, 0.9f, 0.2f, 1.0f}
            });
        });

        // ── 10. Fake extruded text (far-right) ────────────────────────────────
        s.layer("fake_extruded", [](LayerBuilder& l) {
            l.enable_3d()
             .position({460.0f, 150.0f, -240.0f})
             .rotate({14.0f, -20.0f, 0.0f});
            l.fake_extruded_text("extruded", {
                .text       = "AE",
                .font_path  = "assets/fonts/Inter-Bold.ttf",
                .pos        = {0.0f, 0.0f, 0.0f},
                .font_size  = 100.0f,
                .depth      = 40,
                .front_color = Color{1.0f, 1.0f, 1.0f, 1.0f},
                .side_color  = Color{0.1f, 0.35f, 1.0f, 1.0f},
                .align      = TextAlign::Center
            });
        });

        // ── 11. Foreground frame (very near, semi-transparent) ────────────────
        s.layer("foreground_frame", [](LayerBuilder& l) {
            l.enable_3d()
             .position({0.0f, 0.0f, -520.0f});
            l.rounded_rect("frame", {
                .size   = {1100.0f, 580.0f},
                .radius = 32.0f,
                .color  = Color{1.0f, 1.0f, 1.0f, 0.07f},
                .pos    = {0.0f, 0.0f, 0.0f}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("Proof25DObjects", proof_25d_objects)

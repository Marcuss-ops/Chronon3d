// ProofYouTubeDepthTitle v2
//
// Layout disciplinato:
//   background z=700  DOF blur (sfondo sfocato)
//   overlay z=400     dark semi-trasparente
//   card z=0          subject nitido (fuoco DOF)
//   title 2D          testo nitido (no DOF, no 3D)
//   badge 2D          fixed top-right
//
// Frasi su 2 righe. Safe area rispettata.

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>

using namespace chronon3d;

static Composition proof_youtube_depth_title() {
    return composition({
        .name     = "ProofYouTubeDepthTitle",
        .width    = 1280,
        .height   = 720,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const float t  = ctx.duration > 1
            ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1)
            : 0.0f;
        const float st = camera_motion::smoothstep(t);

        // Camera: pan leggero ±20px, dolly lieve, DOF focus sul subject
        s.camera().set({
            .enabled  = true,
            .position = {
                camera_motion::lerp(-20.0f, 20.0f, st),
                0.0f,
                camera_motion::lerp(-1000.0f, -930.0f, st)
            },
            .zoom = 1000.0f,
            .dof  = {.enabled=true, .focus_z=0.0f, .aperture=0.018f, .max_blur=16.0f}
        });

        // ── Background (z=700 → DOF blur ~13px) ──────────────────────────────
        s.layer("bg", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 700.0f});
            l.rect("base", {
                .size  = {2200.0f, 1300.0f},
                .color = Color{0.04f, 0.045f, 0.08f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
            l.circle("bokeh_l", {
                .radius = 280.0f,
                .color  = Color{0.06f, 0.12f, 0.42f, 0.50f},
                .pos    = {-480.0f, -60.0f, 0.0f}
            }).blur(55.0f);
            l.circle("bokeh_r", {
                .radius = 220.0f,
                .color  = Color{0.22f, 0.06f, 0.30f, 0.40f},
                .pos    = {500.0f, 100.0f, 0.0f}
            }).blur(40.0f);
        });

        // ── Dark overlay (z=400) ──────────────────────────────────────────────
        s.layer("overlay", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 400.0f});
            l.rect("v", {
                .size  = {1600.0f, 900.0f},
                .color = Color{0.0f, 0.0f, 0.0f, 0.45f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        // ── Subject card (z=0 — fuoco, nitido) ───────────────────────────────
        s.layer("subject", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, -10.0f, 0.0f});
            l.rounded_rect("card", {
                .size   = {440.0f, 540.0f},
                .radius = 14.0f,
                .color  = Color{0.60f, 0.54f, 0.47f, 1.0f},
                .pos    = {0.0f, 0.0f, 0.0f}
            });
            l.rect("top_zone", {
                .size  = {440.0f, 150.0f},
                .color = Color{0.38f, 0.48f, 0.68f, 0.85f},
                .pos   = {0.0f, -195.0f, 0.0f}
            });
        });

        // ── Title 2D nitido (NO enable_3d → niente DOF) ───────────────────────
        // Posizione in world-centered 2D: (0, 175) = screen (640, 535) — in safe area
        s.layer("title_2d", [](LayerBuilder& l) {
            l.position({0.0f, 175.0f, 0.0f});  // 175 sotto centro
            l.text("line1", {
                .content = "THE TRUTH",
                .style   = {
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size      = 82.0f,
                    .color     = Color{1.0f, 1.0f, 1.0f, 1.0f},
                    .align     = TextAlign::Center,
                },
                .pos = {0.0f, -48.0f, 0.0f}
            });
            l.text("line2", {
                .content = "FINALLY CAME OUT",
                .style   = {
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size      = 82.0f,
                    .color     = Color{1.0f, 0.95f, 0.85f, 1.0f},
                    .align     = TextAlign::Center,
                },
                .pos = {0.0f, 48.0f, 0.0f}
            });
        });

        // ── Badge 2D fisso (absolute screen coords) ───────────────────────────
        s.rect("badge", {
            .size  = {128.0f, 36.0f},
            .color = Color{0.88f, 0.12f, 0.10f, 1.0f},
            .pos   = {1152.0f, 36.0f, 0.0f}  // top-right, dentro safe area
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ProofYouTubeDepthTitle", proof_youtube_depth_title)

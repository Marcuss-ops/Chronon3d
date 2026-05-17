// ProofYouTubeQuoteScene v2
//
// Layout: glass card centrata, testo 2D dentro la card, background DOF sfocato.
// Testo su 2 righe, nitido, nessun blur sul testo.

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>

using namespace chronon3d;

static Composition proof_youtube_quote_scene() {
    return composition({
        .name     = "ProofYouTubeQuoteScene",
        .width    = 1280,
        .height   = 720,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const float t  = ctx.duration > 1
            ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1)
            : 0.0f;
        const float st = camera_motion::smoothstep(t);

        // Camera: dolly leggero, DOF per sfondo
        s.camera().set({
            .enabled  = true,
            .position = {0.0f, 0.0f, camera_motion::lerp(-1000.0f, -950.0f, st)},
            .zoom     = 1000.0f,
            .dof      = {.enabled=true, .focus_z=0.0f, .aperture=0.016f, .max_blur=14.0f}
        });

        // ── Background sfocato (z=700 → DOF ~11px) ───────────────────────────
        s.layer("bg", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 700.0f});
            l.rect("base", {
                .size  = {2200.0f, 1300.0f},
                .color = Color{0.04f, 0.04f, 0.06f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
            l.circle("glow_l", {
                .radius = 260.0f,
                .color  = Color{0.06f, 0.05f, 0.22f, 0.45f},
                .pos    = {-430.0f, -40.0f, 0.0f}
            }).blur(60.0f);
            l.circle("glow_r", {
                .radius = 180.0f,
                .color  = Color{0.16f, 0.04f, 0.08f, 0.40f},
                .pos    = {420.0f,  70.0f, 0.0f}
            }).blur(45.0f);
        });

        // ── Glass card (z=0 — fuoco) ──────────────────────────────────────────
        s.layer("card", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 0.0f});
            l.rounded_rect("glass", {
                .size   = {880.0f, 240.0f},
                .radius = 12.0f,
                .color  = Color{0.05f, 0.05f, 0.08f, 0.80f},
                .pos    = {0.0f, 0.0f, 0.0f}
            });
            // Linea accent superiore
            l.rect("accent", {
                .size  = {760.0f, 3.0f},
                .color = Color{0.55f, 0.40f, 0.90f, 0.85f},
                .pos   = {0.0f, -105.0f, 0.0f}
            });
        });

        // ── Quote text 2D nitido (NO DOF, centrato nella card) ────────────────
        // (0, 0) in 2D = screen center (640, 360)
        s.layer("quote", [](LayerBuilder& l) {
            l.position({0.0f, 0.0f, 0.0f});  // screen center
            l.text("q1", {
                .content = "EVERYTHING",
                .style   = {
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size      = 76.0f,
                    .color     = Color{1.0f, 1.0f, 1.0f, 1.0f},
                    .align     = TextAlign::Center,
                },
                .pos = {0.0f, -46.0f, 0.0f}
            });
            l.text("q2", {
                .content = "CHANGED OVERNIGHT",
                .style   = {
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size      = 64.0f,
                    .color     = Color{0.85f, 0.75f, 1.0f, 1.0f},
                    .align     = TextAlign::Center,
                },
                .pos = {0.0f, 46.0f, 0.0f}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ProofYouTubeQuoteScene", proof_youtube_quote_scene)

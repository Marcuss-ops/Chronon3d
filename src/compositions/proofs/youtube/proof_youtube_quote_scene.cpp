// ProofYouTubeQuoteScene — scena narrativa con citazione centrata.
//
// Struttura:
//   z=600  background scuro sfocato
//   z=0    backdrop card semi-trasparente
//   z=-100 testo citazione grande
//
// Camera: dolly-in molto lento (90 frame)
//
//   chronon3d_cli video ProofYouTubeQuoteScene --graph --start 0 --end 89 --fps 30 -o output/proofs/yt_quote.mp4

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

        // Camera: dolly-in lento, leggero DOF per sfondo
        s.camera().set({
            .enabled  = true,
            .position = {0.0f, 0.0f, camera_motion::lerp(-1000.0f, -940.0f, st)},
            .zoom     = 1000.0f,
            .dof      = {
                .enabled  = true,
                .focus_z  = 0.0f,
                .aperture = 0.018f,
                .max_blur = 16.0f
            }
        });

        // ── Background (z=600 → DOF blur ~14px) ──────────────────────────────
        s.layer("bg", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 600.0f});

            l.rect("base", {
                .size  = {2000.0f, 1200.0f},
                .color = Color{0.04f, 0.04f, 0.06f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });

            // Forma luminosa sinistra
            l.circle("accent_l", {
                .radius = 280.0f,
                .color  = Color{0.08f, 0.06f, 0.20f, 0.5f},
                .pos    = {-500.0f, -50.0f, 0.0f}
            }).blur(60.0f);

            // Forma luminosa destra
            l.circle("accent_r", {
                .radius = 200.0f,
                .color  = Color{0.18f, 0.04f, 0.08f, 0.45f},
                .pos    = {480.0f, 80.0f, 0.0f}
            }).blur(45.0f);
        });

        // ── Backdrop card (z=0 — semi-trasparente) ────────────────────────────
        s.layer("backdrop", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 0.0f});
            l.rounded_rect("card", {
                .size   = {900.0f, 260.0f},
                .radius = 8.0f,
                .color  = Color{0.04f, 0.04f, 0.06f, 0.78f},
                .pos    = {0.0f, 0.0f, 0.0f}
            });

            // Linea decorativa superiore
            l.rect("line_top", {
                .size  = {800.0f, 3.0f},
                .color = Color{0.6f, 0.5f, 0.9f, 0.8f},
                .pos   = {0.0f, -115.0f, 0.0f}
            });
        });

        // ── Testo citazione (z=-100) ──────────────────────────────────────────
        s.layer("quote", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, -10.0f, -100.0f});
            l.text("q1", {
                .content = "EVERYTHING CHANGED OVERNIGHT",
                .style   = {
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size      = 72.0f,
                    .color     = Color{1.0f, 1.0f, 1.0f, 1.0f},
                    .align     = TextAlign::Center,
                },
                .pos = {0.0f, -20.0f, 0.0f}
            }).with_glow(Glow{
                .enabled   = true,
                .radius    = 8.0f,
                .intensity = 0.25f,
                .color     = Color{0.5f, 0.4f, 0.9f, 1.0f}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ProofYouTubeQuoteScene", proof_youtube_quote_scene)

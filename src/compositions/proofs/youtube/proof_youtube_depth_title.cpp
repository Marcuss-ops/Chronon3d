// ProofYouTubeDepthTitle — scena YouTube realistica: background sfocato, subject
// centrale, titolo in primo piano, badge 2D fisso.
//
// Struttura z-depth:
//   z=700  background bokeh sfocato (DOF)
//   z=0    subject card (nitido — punto di fuoco)
//   z=-200 titolo testo davanti (lieve blur DOF)
//   2D     badge "PROOF" fisso
//
// Camera: pan -40→+40 + dolly -1000→-850 (90 frame)
//
//   chronon3d_cli render ProofYouTubeDepthTitle --graph --frames 0  -o output/proofs/yt_depth_title_f000.png
//   chronon3d_cli render ProofYouTubeDepthTitle --graph --frames 45 -o output/proofs/yt_depth_title_f045.png
//   chronon3d_cli video  ProofYouTubeDepthTitle --graph --start 0 --end 89 --fps 30 -o output/proofs/yt_depth_title.mp4

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

        // Camera: pan + dolly, DOF con fuoco sul subject
        s.camera().set({
            .enabled  = true,
            .position = {
                camera_motion::lerp(-40.0f, 40.0f, st),
                0.0f,
                camera_motion::lerp(-1000.0f, -850.0f, st)
            },
            .zoom = 1000.0f,
            .dof  = {
                .enabled  = true,
                .focus_z  = 0.0f,
                .aperture = 0.022f,
                .max_blur = 20.0f
            }
        });

        // ── Background (z=700, DOF blur automatico) ───────────────────────────
        s.layer("bg", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 700.0f});

            // Base scura
            l.rect("base", {
                .size  = {2200.0f, 1300.0f},
                .color = Color{0.04f, 0.045f, 0.08f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });

            // Bokeh blu — forma illuminata sinistra
            l.circle("bokeh_l", {
                .radius = 320.0f,
                .color  = Color{0.05f, 0.12f, 0.45f, 0.55f},
                .pos    = {-500.0f, -80.0f, 0.0f}
            }).blur(50.0f);

            // Bokeh viola — sinistra-bassa
            l.circle("bokeh_m", {
                .radius = 220.0f,
                .color  = Color{0.25f, 0.06f, 0.35f, 0.45f},
                .pos    = {-200.0f, 200.0f, 0.0f}
            }).blur(35.0f);

            // Bokeh caldo — destra
            l.circle("bokeh_r", {
                .radius = 280.0f,
                .color  = Color{0.45f, 0.12f, 0.05f, 0.35f},
                .pos    = {550.0f, -120.0f, 0.0f}
            }).blur(45.0f);
        });

        // ── Dark overlay (z=400, semi-trasparente, aumenta contrasto) ─────────
        s.layer("overlay", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 400.0f});
            l.rect("vignette", {
                .size  = {1600.0f, 900.0f},
                .color = Color{0.0f, 0.0f, 0.0f, 0.50f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        // ── Subject card (z=0 — fuoco DOF, nitido) ───────────────────────────
        s.layer("subject", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, -30.0f, 0.0f});

            // Card principale (simula immagine/personaggio)
            l.rounded_rect("card", {
                .size   = {480.0f, 580.0f},
                .radius = 16.0f,
                .color  = Color{0.62f, 0.55f, 0.48f, 1.0f},
                .pos    = {0.0f, 0.0f, 0.0f}
            });

            // Striscia chiara in alto (simula zona capelli/cielo)
            l.rect("card_top", {
                .size  = {480.0f, 160.0f},
                .color = Color{0.45f, 0.50f, 0.70f, 0.8f},
                .pos   = {0.0f, -210.0f, 0.0f}
            });

            // Striscia scura in basso (simula vestito/ombra)
            l.rect("card_bot", {
                .size  = {480.0f, 120.0f},
                .color = Color{0.12f, 0.10f, 0.09f, 0.9f},
                .pos   = {0.0f, 230.0f, 0.0f}
            });
        });

        // ── Titolo in primo piano (z=-200, lieve DOF blur) ───────────────────
        s.layer("title", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 200.0f, -200.0f});
            l.text("headline", {
                .content = "THE TRUTH FINALLY CAME OUT",
                .style   = {
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size      = 88.0f,
                    .color     = Color{1.0f, 1.0f, 1.0f, 1.0f},
                    .align     = TextAlign::Center,
                },
                .pos = {0.0f, 0.0f, 0.0f}
            }).with_glow(Glow{
                .enabled   = true,
                .radius    = 10.0f,
                .intensity = 0.35f,
                .color     = Color{0.0f, 0.0f, 0.0f, 1.0f}
            });
        });

        // ── Badge 2D fisso (no enable_3d) ─────────────────────────────────────
        s.rect("badge_bg", {
            .size  = {120.0f, 38.0f},
            .color = Color{0.90f, 0.15f, 0.12f, 1.0f},
            .pos   = {1160.0f, 40.0f, 0.0f}  // top-right, screen coords
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ProofYouTubeDepthTitle", proof_youtube_depth_title)

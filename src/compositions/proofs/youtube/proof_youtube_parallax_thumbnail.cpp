// ProofYouTubeParallaxThumbnail — thumbnail animata con parallax realistico.
//
// Struttura:
//   z=800  background paesaggio colorato (si muove poco)
//   z=0    subject card centrale (si muove normale)
//   z=-300 testo davanti grande (si muove di più)
//
// Camera: pan -60→+60 + dolly -1000→-880
//
//   chronon3d_cli video ProofYouTubeParallaxThumbnail --graph --start 0 --end 89 --fps 30 -o output/proofs/yt_parallax.mp4

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>

using namespace chronon3d;

static Composition proof_youtube_parallax_thumbnail() {
    return composition({
        .name     = "ProofYouTubeParallaxThumbnail",
        .width    = 1280,
        .height   = 720,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const float t  = ctx.duration > 1
            ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1)
            : 0.0f;
        const float st = camera_motion::smoothstep(t);

        // Camera: pan ampio + dolly-in
        s.camera().set({
            .enabled  = true,
            .position = {
                camera_motion::lerp(-60.0f, 60.0f, st),
                0.0f,
                camera_motion::lerp(-1000.0f, -880.0f, st)
            },
            .zoom = 1000.0f,
        });

        // ── Background paesaggio (z=800 — si muove poco) ──────────────────────
        s.layer("landscape", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 800.0f});

            // Cielo sfumato
            l.rect("sky", {
                .size  = {2800.0f, 1600.0f},
                .color = Color{0.05f, 0.08f, 0.18f, 1.0f},
                .pos   = {0.0f, -200.0f, 0.0f}
            });

            // Zona orizzonte (striscia luce calda)
            l.rect("horizon", {
                .size  = {2800.0f, 200.0f},
                .color = Color{0.55f, 0.28f, 0.08f, 0.65f},
                .pos   = {0.0f, 100.0f, 0.0f}
            }).blur(40.0f);

            // Terra scura in basso
            l.rect("ground", {
                .size  = {2800.0f, 500.0f},
                .color = Color{0.04f, 0.04f, 0.05f, 1.0f},
                .pos   = {0.0f, 450.0f, 0.0f}
            });

            // Forme silhouette (montagne/edifici)
            l.rect("sil1", {
                .size  = {200.0f, 280.0f},
                .color = Color{0.06f, 0.06f, 0.08f, 1.0f},
                .pos   = {-600.0f, 60.0f, 0.0f}
            });
            l.rect("sil2", {
                .size  = {160.0f, 320.0f},
                .color = Color{0.06f, 0.06f, 0.08f, 1.0f},
                .pos   = {-430.0f, 40.0f, 0.0f}
            });
            l.rect("sil3", {
                .size  = {280.0f, 360.0f},
                .color = Color{0.05f, 0.05f, 0.07f, 1.0f},
                .pos   = { 350.0f, 30.0f, 0.0f}
            });
        });

        // ── Subject card (z=0 — si muove normalmente) ─────────────────────────
        s.layer("subject", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, -30.0f, 0.0f});

            l.rounded_rect("cutout", {
                .size   = {400.0f, 540.0f},
                .radius = 12.0f,
                .color  = Color{0.65f, 0.58f, 0.50f, 1.0f},
                .pos    = {0.0f, 0.0f, 0.0f}
            });

            // Highlight bordo (simula ritaglio/cutout persona)
            l.rounded_rect("outline", {
                .size   = {412.0f, 552.0f},
                .radius = 14.0f,
                .color  = Color{1.0f, 0.9f, 0.7f, 0.20f},
                .pos    = {0.0f, 0.0f, 0.0f}
            });

            // Zona viso (striscia chiara alta)
            l.rect("face", {
                .size  = {380.0f, 180.0f},
                .color = Color{0.82f, 0.72f, 0.62f, 1.0f},
                .pos   = {0.0f, -170.0f, 0.0f}
            });
        });

        // ── Titolo davanti (z=-300 — si muove di più) ─────────────────────────
        s.layer("title", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 240.0f, -300.0f});
            l.text("headline", {
                .content = "THE MOMENT THAT SHOCKED EVERYONE",
                .style   = {
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size      = 68.0f,
                    .color     = Color{1.0f, 0.95f, 0.20f, 1.0f},
                    .align     = TextAlign::Center,
                },
                .pos = {0.0f, 0.0f, 0.0f}
            }).with_glow(Glow{
                .enabled   = true,
                .radius    = 14.0f,
                .intensity = 0.45f,
                .color     = Color{0.0f, 0.0f, 0.0f, 1.0f}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ProofYouTubeParallaxThumbnail", proof_youtube_parallax_thumbnail)

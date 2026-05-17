// ProofYouTubeNewsCard — layout news/gossip: immagine sinistra, headline destra,
// lower third, badge "BREAKING" 2D fisso.
//
// Test:
//   - layout stabile tra frame 0 e 89
//   - badge 2D invariato (no camera)
//   - background più sfocato del subject
//
//   chronon3d_cli video ProofYouTubeNewsCard --graph --start 0 --end 89 --fps 30 -o output/proofs/yt_news_card.mp4

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>

using namespace chronon3d;

static Composition proof_youtube_news_card() {
    return composition({
        .name     = "ProofYouTubeNewsCard",
        .width    = 1280,
        .height   = 720,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const float t  = ctx.duration > 1
            ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1)
            : 0.0f;
        const float st = camera_motion::smoothstep(t);

        // Camera: pan leggero, DOF per sfondo
        s.camera().set({
            .enabled  = true,
            .position = {camera_motion::lerp(-20.0f, 20.0f, st), 0.0f, -1000.0f},
            .zoom     = 1000.0f,
            .dof      = {
                .enabled  = true,
                .focus_z  = 0.0f,
                .aperture = 0.020f,
                .max_blur = 18.0f
            }
        });

        // ── Background sfocato (z=600) ────────────────────────────────────────
        s.layer("bg", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 600.0f});
            l.rect("base", {
                .size  = {2000.0f, 1200.0f},
                .color = Color{0.06f, 0.05f, 0.07f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
            l.circle("bg_accent", {
                .radius = 300.0f,
                .color  = Color{0.10f, 0.05f, 0.05f, 0.4f},
                .pos    = {400.0f, -100.0f, 0.0f}
            }).blur(60.0f);
        });

        // ── Image card sinistra (z=0) ─────────────────────────────────────────
        s.layer("image_card", [](LayerBuilder& l) {
            l.enable_3d().position({-290.0f, 0.0f, 0.0f});

            l.rounded_rect("portrait", {
                .size   = {480.0f, 580.0f},
                .radius = 10.0f,
                .color  = Color{0.55f, 0.50f, 0.45f, 1.0f},
                .pos    = {0.0f, 0.0f, 0.0f}
            });
            // Zona "capelli" in alto
            l.rect("hair", {
                .size  = {480.0f, 160.0f},
                .color = Color{0.18f, 0.14f, 0.10f, 1.0f},
                .pos   = {0.0f, -210.0f, 0.0f}
            });
        });

        // ── Headline testo destra (z=-100) ────────────────────────────────────
        s.layer("headline", [](LayerBuilder& l) {
            l.enable_3d().position({260.0f, -80.0f, -100.0f});
            l.text("h1", {
                .content = "BREAKING NEWS",
                .style   = {
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size      = 72.0f,
                    .color     = Color{1.0f, 0.95f, 0.9f, 1.0f},
                    .align     = TextAlign::Left,
                },
                .pos = {0.0f, -40.0f, 0.0f}
            });
            l.text("h2", {
                .content = "SHOCKING DETAILS REVEALED",
                .style   = {
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size      = 44.0f,
                    .color     = Color{0.85f, 0.82f, 0.78f, 1.0f},
                    .align     = TextAlign::Left,
                },
                .pos = {0.0f, 40.0f, 0.0f}
            });
        });

        // ── Lower third bar (z=-50) ───────────────────────────────────────────
        s.layer("lower_third", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 290.0f, -50.0f});
            l.rect("bar", {
                .size  = {1100.0f, 52.0f},
                .color = Color{0.88f, 0.12f, 0.10f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        // ── Badge "BREAKING" 2D fisso (screen coords) ─────────────────────────
        s.rect("badge", {
            .size  = {180.0f, 42.0f},
            .color = Color{0.92f, 0.10f, 0.08f, 1.0f},
            .pos   = {110.0f, 36.0f, 0.0f}   // top-left, absolute screen coords
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ProofYouTubeNewsCard", proof_youtube_news_card)

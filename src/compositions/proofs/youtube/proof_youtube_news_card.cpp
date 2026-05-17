// ProofYouTubeNewsCard v2
//
// Layout news disciplinato:
//   immagine sinistra (z=0)
//   headline 2D destra (nitida, inside safe area)
//   lower-third bar (z=-50)
//   badge "BREAKING" 2D fixed top-left
//
// Safe area: x 80-1200, y 80-640

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

        // Camera: pan minimo ±15px, DOF per sfondo
        s.camera().set({
            .enabled  = true,
            .position = {camera_motion::lerp(-15.0f, 15.0f, st), 0.0f, -1000.0f},
            .zoom     = 1000.0f,
            .dof      = {.enabled=true, .focus_z=0.0f, .aperture=0.016f, .max_blur=14.0f}
        });

        // ── Background sfocato (z=600) ────────────────────────────────────────
        s.layer("bg", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 600.0f});
            l.rect("base", {
                .size  = {2000.0f, 1200.0f},
                .color = Color{0.05f, 0.05f, 0.07f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
            l.circle("glow", {
                .radius = 260.0f,
                .color  = Color{0.10f, 0.05f, 0.05f, 0.38f},
                .pos    = {350.0f, -80.0f, 0.0f}
            }).blur(60.0f);
        });

        // ── Image card sinistra (z=0, nitida) ─────────────────────────────────
        // Centrata mondo (0,0) = screen center (640,360)
        // Card a sinistra: mondo x=-240 = screen x=400. width=380 → x: 210-590
        s.layer("image_card", [](LayerBuilder& l) {
            l.enable_3d().position({-240.0f, 0.0f, 0.0f});
            l.rounded_rect("portrait", {
                .size   = {380.0f, 520.0f},
                .radius = 10.0f,
                .color  = Color{0.55f, 0.50f, 0.45f, 1.0f},
                .pos    = {0.0f, 0.0f, 0.0f}
            });
            l.rect("hair", {
                .size  = {380.0f, 140.0f},
                .color = Color{0.16f, 0.12f, 0.09f, 1.0f},
                .pos   = {0.0f, -190.0f, 0.0f}
            });
        });

        // ── Lower-third bar (z=-50, leggero DOF blur ~0.9px — trascurabile) ───
        s.layer("lower_third", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 250.0f, -50.0f});
            l.rect("bar", {
                .size  = {1100.0f, 48.0f},
                .color = Color{0.88f, 0.12f, 0.10f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        // ── Headline 2D nitido (destra, dentro safe area) ─────────────────────
        // mondo-2D x=170 = screen x=810. Testo align Left, max ~480px → fino a 1200 safe
        s.layer("headline_2d", [](LayerBuilder& l) {
            l.position({170.0f, -70.0f, 0.0f});  // destra, lievemente sopra centro
            l.text("h1", {
                .content = "BREAKING",
                .style   = {
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size      = 68.0f,
                    .color     = Color{1.0f, 0.92f, 0.88f, 1.0f},
                    .align     = TextAlign::Left,
                },
                .pos = {0.0f, -44.0f, 0.0f}
            });
            l.text("h2", {
                .content = "SHOCKING DETAILS",
                .style   = {
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size      = 46.0f,
                    .color     = Color{0.82f, 0.78f, 0.75f, 1.0f},
                    .align     = TextAlign::Left,
                },
                .pos = {0.0f, 20.0f, 0.0f}
            });
        });

        // ── Badge "BREAKING" 2D fixed top-left ───────────────────────────────
        s.rect("badge", {
            .size  = {168.0f, 38.0f},
            .color = Color{0.90f, 0.10f, 0.08f, 1.0f},
            .pos   = {164.0f, 36.0f, 0.0f}  // screen coords: left edge at x=80 (safe area)
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ProofYouTubeNewsCard", proof_youtube_news_card)

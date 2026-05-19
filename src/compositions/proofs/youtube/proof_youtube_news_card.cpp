// ProofYouTubeNewsCard v2 (text removed — text engine deprecated)
//
// Layout news disciplinato:
//   immagine sinistra (z=0)
//   lower-third bar (z=-50)
//   badge "BREAKING" 2D fixed top-left

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

        s.camera().set({
            .enabled  = true,
            .position = {camera_motion::lerp(-15.0f, 15.0f, st), 0.0f, -1000.0f},
            .zoom     = 1000.0f,
            .dof      = {.enabled=true, .focus_z=0.0f, .aperture=0.016f, .max_blur=14.0f}
        });

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

        s.layer("lower_third", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 250.0f, -50.0f});
            l.rect("bar", {
                .size  = {1100.0f, 48.0f},
                .color = Color{0.88f, 0.12f, 0.10f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        // Headline text removed — text engine deprecated

        s.rect("badge", {
            .size  = {168.0f, 38.0f},
            .color = Color{0.90f, 0.10f, 0.08f, 1.0f},
            .pos   = {164.0f, 36.0f, 0.0f}
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ProofYouTubeNewsCard", proof_youtube_news_card)

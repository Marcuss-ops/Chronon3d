// ProofYouTubeParallaxThumbnail v2
//
// Thumbnail 3D con parallax visibile:
//   background z=800 (si muove poco)
//   subject z=0 (si muove normale)
//   title 2D nitido (si muove con parallax 2D fisso rispetto a camera)
//
// Testo spezzato in 2 righe, dentro safe area.
// Nessun DOF — questo proof testa solo il parallax.

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

        // Camera: pan ±40px + dolly leggero (no DOF per vedere il parallax netto)
        s.camera().set({
            .enabled  = true,
            .position = {
                camera_motion::lerp(-40.0f, 40.0f, st),
                0.0f,
                camera_motion::lerp(-1000.0f, -920.0f, st)
            },
            .zoom = 1000.0f
        });

        // ── Background paesaggio (z=800 — si sposta poco con pan) ────────────
        s.layer("landscape", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 800.0f});

            // Cielo notturno
            l.rect("sky", {
                .size  = {2800.0f, 1600.0f},
                .color = Color{0.04f, 0.06f, 0.16f, 1.0f},
                .pos   = {0.0f, -200.0f, 0.0f}
            });
            // Orizzonte caldo
            l.rect("horizon", {
                .size  = {2800.0f, 160.0f},
                .color = Color{0.50f, 0.24f, 0.06f, 0.60f},
                .pos   = {0.0f, 100.0f, 0.0f}
            }).blur(30.0f);
            // Terra scura
            l.rect("ground", {
                .size  = {2800.0f, 500.0f},
                .color = Color{0.03f, 0.03f, 0.04f, 1.0f},
                .pos   = {0.0f, 450.0f, 0.0f}
            });
            // Silhouette edifici
            l.rect("s1", {.size={180.0f,300.0f}, .color={0.05f,0.05f,0.07f,1.0f}, .pos={-550.0f, 50.0f, 0.0f}});
            l.rect("s2", {.size={240.0f,380.0f}, .color={0.04f,0.04f,0.06f,1.0f}, .pos={  50.0f, 10.0f, 0.0f}});
            l.rect("s3", {.size={160.0f,260.0f}, .color={0.05f,0.05f,0.07f,1.0f}, .pos={ 450.0f, 70.0f, 0.0f}});
        });

        // ── Subject card (z=0 — si muove normalmente) ─────────────────────────
        s.layer("subject", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, -20.0f, 0.0f});
            l.rounded_rect("cutout", {
                .size   = {400.0f, 520.0f},
                .radius = 12.0f,
                .color  = Color{0.62f, 0.55f, 0.47f, 1.0f},
                .pos    = {0.0f, 0.0f, 0.0f}
            });
            // Highlight bordo sottile
            l.rounded_rect("rim", {
                .size   = {412.0f, 532.0f},
                .radius = 14.0f,
                .color  = Color{1.0f, 0.88f, 0.60f, 0.18f},
                .pos    = {0.0f, 0.0f, 0.0f}
            });
            l.rect("face", {
                .size  = {380.0f, 170.0f},
                .color = Color{0.80f, 0.70f, 0.60f, 1.0f},
                .pos   = {0.0f, -175.0f, 0.0f}
            });
        });

        // ── Titolo 2D nitido — spezzato in 2 righe, in basso safe area ────────
        // mondo-2D y=170 = screen y=530 (dentro safe area y<640)
        s.layer("title_2d", [](LayerBuilder& l) {
            l.position({0.0f, 170.0f, 0.0f});
            l.text("t1", {
                .content = "THE MOMENT THAT",
                .style   = {
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size      = 72.0f,
                    .color     = Color{1.0f, 0.92f, 0.18f, 1.0f},
                    .align     = TextAlign::Center,
                },
                .pos = {0.0f, -44.0f, 0.0f}
            }).with_glow(Glow{.enabled=true, .radius=10.0f, .intensity=0.30f, .color={0,0,0,1}});
            l.text("t2", {
                .content = "SHOCKED EVERYONE",
                .style   = {
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size      = 72.0f,
                    .color     = Color{1.0f, 0.92f, 0.18f, 1.0f},
                    .align     = TextAlign::Center,
                },
                .pos = {0.0f, 44.0f, 0.0f}
            }).with_glow(Glow{.enabled=true, .radius=10.0f, .intensity=0.30f, .color={0,0,0,1}});
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ProofYouTubeParallaxThumbnail", proof_youtube_parallax_thumbnail)

// ProofYouTubeImageDof — testa il DOF con tre piani chiaramente separati.
//
//   z=800  background: skyline geometrico (DOF blur forte ~18px)
//   z=0    subject card (fuoco — nitido)
//   z=-300 particelle foreground (DOF blur lieve ~7px)
//
// Camera: statica, solo DOF. Test visivo puro.
//
//   chronon3d_cli render ProofYouTubeImageDof --graph --frames 0 -o output/proofs/yt_dof_f000.png

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>

using namespace chronon3d;

static Composition proof_youtube_image_dof() {
    return composition({
        .name     = "ProofYouTubeImageDof",
        .width    = 1280,
        .height   = 720,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Camera statica con DOF forte, fuoco sul subject (z=0)
        s.camera().set({
            .enabled  = true,
            .position = {0.0f, 0.0f, -1000.0f},
            .zoom     = 1000.0f,
            .dof      = {
                .enabled  = true,
                .focus_z  = 0.0f,
                .aperture = 0.025f,
                .max_blur = 24.0f
            }
        });

        // ── Background skyline (z=800 → blur DOF ~20px) ───────────────────────
        s.layer("bg", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 800.0f});

            // Cielo notturno
            l.rect("sky", {
                .size  = {2400.0f, 1400.0f},
                .color = Color{0.03f, 0.04f, 0.10f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });

            // Grattacieli — forme geometriche (edge visibili → DOF le sfalsa)
            struct Building { float x, y, w, h; Color c; };
            static const Building buildings[] = {
                {-580.0f, 80.0f,  90.0f, 380.0f, {0.12f, 0.14f, 0.22f, 1.0f}},
                {-440.0f, 120.0f, 70.0f, 330.0f, {0.10f, 0.12f, 0.20f, 1.0f}},
                {-310.0f, 50.0f,  110.0f, 440.0f, {0.14f, 0.16f, 0.24f, 1.0f}},
                { -80.0f, 90.0f,  80.0f, 390.0f, {0.11f, 0.13f, 0.21f, 1.0f}},
                {  60.0f, 20.0f,  130.0f, 480.0f, {0.16f, 0.18f, 0.28f, 1.0f}},
                { 240.0f, 110.0f,  75.0f, 350.0f, {0.10f, 0.12f, 0.20f, 1.0f}},
                { 370.0f, 60.0f,   95.0f, 420.0f, {0.13f, 0.15f, 0.23f, 1.0f}},
                { 530.0f, 140.0f,  60.0f, 300.0f, {0.09f, 0.11f, 0.19f, 1.0f}},
            };
            int i = 0;
            for (const auto& b : buildings) {
                l.rect("bld" + std::to_string(i++), {
                    .size  = {b.w, b.h},
                    .color = b.c,
                    .pos   = {b.x, b.y, 0.0f}
                });
            }

            // Luci finestre (piccoli punti luminosi — visibilissimi sfocati)
            l.circle("light1", {.radius = 8.0f, .color = {0.9f, 0.85f, 0.5f, 1.0f}, .pos = {-430.0f,  30.0f, 0.0f}});
            l.circle("light2", {.radius = 6.0f, .color = {0.8f, 0.75f, 0.4f, 1.0f}, .pos = { -55.0f,  10.0f, 0.0f}});
            l.circle("light3", {.radius = 7.0f, .color = {0.9f, 0.82f, 0.5f, 1.0f}, .pos = {  95.0f, -40.0f, 0.0f}});
            l.circle("light4", {.radius = 5.0f, .color = {0.85f, 0.8f, 0.45f, 1.0f}, .pos = { 380.0f, -30.0f, 0.0f}});
        });

        // ── Subject (z=0 — fuoco, massima nitidezza) ──────────────────────────
        s.layer("subject", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 0.0f});

            // Card principale
            l.rounded_rect("card", {
                .size   = {420.0f, 520.0f},
                .radius = 12.0f,
                .color  = Color{0.70f, 0.63f, 0.55f, 1.0f},
                .pos    = {0.0f, 0.0f, 0.0f}
            });

            // Barra chiara in alto
            l.rect("top", {
                .size  = {420.0f, 140.0f},
                .color = Color{0.40f, 0.52f, 0.72f, 0.9f},
                .pos   = {0.0f, -190.0f, 0.0f}
            });

            // Bordo bianco (nitidezza edge)
            l.rounded_rect("border", {
                .size   = {430.0f, 530.0f},
                .radius = 14.0f,
                .color  = Color{1.0f, 1.0f, 1.0f, 0.12f},
                .pos    = {0.0f, 0.0f, 0.0f}
            });
        });

        // ── Foreground particelle (z=-300 → blur DOF ~7px) ───────────────────
        s.layer("foreground", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, -300.0f});

            const Color pc{0.8f, 0.85f, 1.0f, 0.55f};
            l.circle("p1", {.radius = 22.0f, .color = pc, .pos = {-520.0f,  180.0f, 0.0f}});
            l.circle("p2", {.radius = 18.0f, .color = pc, .pos = { 460.0f, -220.0f, 0.0f}});
            l.circle("p3", {.radius = 14.0f, .color = pc, .pos = {-350.0f, -260.0f, 0.0f}});
            l.circle("p4", {.radius = 26.0f, .color = pc, .pos = { 540.0f,  240.0f, 0.0f}});
            l.circle("p5", {.radius = 16.0f, .color = pc, .pos = {-600.0f,   -80.0f, 0.0f}});
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ProofYouTubeImageDof", proof_youtube_image_dof)

// ProofYouTubeImageDof v2
//
// Test DOF a tre piani ben separati:
//   z=800  background skyline (blur DOF forte ~20px)
//   z=0    subject card (fuoco — nitido)
//   z=-300 particelle foreground (blur DOF medio ~7px)
//   2D     etichetta "FOCUS SUBJECT" nitida
//
// Sfondo con grattacieli e luci finestre per rendere l'edge DOF visibile.

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

        // Camera statica con DOF forte
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

        // ── Background skyline (z=800 → DOF blur ~20px) ───────────────────────
        s.layer("bg", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 800.0f});
            l.rect("sky", {
                .size  = {2400.0f, 1400.0f},
                .color = Color{0.03f, 0.04f, 0.10f, 1.0f},
                .pos   = {0.0f, -200.0f, 0.0f}
            });
            // Grattacieli con edge ben definite (DOF le sfalsa)
            struct B { float x, y, w, h; Color c; };
            static const B blds[] = {
                {-600, 60, 100, 400, {0.12f,0.14f,0.22f,1}},
                {-450, 30,  80, 460, {0.10f,0.12f,0.20f,1}},
                {-300, 70, 120, 380, {0.14f,0.16f,0.24f,1}},
                { -80, 20, 140, 500, {0.16f,0.18f,0.28f,1}},
                {  90, 80,  90, 390, {0.11f,0.13f,0.21f,1}},
                { 250, 50,  100,440, {0.13f,0.15f,0.23f,1}},
                { 420,120,   70,310, {0.09f,0.11f,0.19f,1}},
                { 560, 40,  110,470, {0.14f,0.16f,0.24f,1}},
            };
            int i = 0;
            for (const auto& b : blds) {
                l.rect("b"+std::to_string(i++),
                    {.size={b.w,b.h}, .color=b.c, .pos={b.x,b.y,0.0f}});
            }
            // Luci finestre — bokeh sfocati dal DOF
            const Color lc{0.95f,0.88f,0.5f,1.0f};
            l.circle("lw1",{.radius=6.0f,.color=lc,.pos={-440.0f,-30.0f,0.0f}});
            l.circle("lw2",{.radius=5.0f,.color=lc,.pos={ -60.0f,-80.0f,0.0f}});
            l.circle("lw3",{.radius=7.0f,.color=lc,.pos={  110.0f,-50.0f,0.0f}});
            l.circle("lw4",{.radius=5.0f,.color=lc,.pos={  390.0f,-20.0f,0.0f}});
            l.circle("lw5",{.radius=6.0f,.color=lc,.pos={  580.0f,-60.0f,0.0f}});
        });

        // ── Subject (z=0 — fuoco, nitido) ─────────────────────────────────────
        s.layer("subject", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 0.0f});
            l.rounded_rect("card", {
                .size   = {440.0f, 540.0f},
                .radius = 12.0f,
                .color  = Color{0.68f, 0.61f, 0.53f, 1.0f},
                .pos    = {0.0f, 0.0f, 0.0f}
            });
            l.rect("top", {
                .size  = {440.0f, 150.0f},
                .color = Color{0.38f, 0.50f, 0.70f, 0.9f},
                .pos   = {0.0f, -195.0f, 0.0f}
            });
            // Edge contrast strip (nitida — conferma focus)
            l.rect("edge_strip", {
                .size  = {440.0f, 4.0f},
                .color = Color{1.0f, 1.0f, 1.0f, 0.55f},
                .pos   = {0.0f, -168.0f, 0.0f}
            });
        });

        // ── Foreground particelle (z=-300 → DOF blur ~7px) ───────────────────
        s.layer("fg", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, -300.0f});
            const Color pc{0.75f, 0.82f, 1.0f, 0.50f};
            l.circle("p1",{.radius=24.0f,.color=pc,.pos={-520.0f, 180.0f,0.0f}});
            l.circle("p2",{.radius=18.0f,.color=pc,.pos={ 480.0f,-220.0f,0.0f}});
            l.circle("p3",{.radius=14.0f,.color=pc,.pos={-360.0f,-250.0f,0.0f}});
            l.circle("p4",{.radius=28.0f,.color=pc,.pos={ 560.0f, 240.0f,0.0f}});
            l.circle("p5",{.radius=16.0f,.color=pc,.pos={-580.0f, -80.0f,0.0f}});
        });

        // ── Label 2D nitida (conferma visivamente dove è il fuoco) ───────────
        s.layer("label_2d", [](LayerBuilder& l) {
            l.position({0.0f, -220.0f, 0.0f});  // sopra la card (mondo-2D y=-220 = screen y=140)
            l.text("lbl", {
                .content = "FOCUS SUBJECT",
                .style   = {
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .size      = 32.0f,
                    .color     = Color{0.6f, 1.0f, 0.6f, 0.85f},
                    .align     = TextAlign::Center,
                },
                .pos = {0.0f, 0.0f, 0.0f}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ProofYouTubeImageDof", proof_youtube_image_dof)

// ProofPanParallax — verifica pan orizzontale e parallasse.
//
// Camera pana da -120 a +120 in X.
// Tre card a profondità diverse: l'oggetto vicino si sposta di più in screen space.
//
//   FAR  z=+600  (blu)   — si sposta poco
//   MID  z=0     (verde) — si sposta normalmente
//   NEAR z=-300  (rosso) — si sposta di più
//
// Se tutti gli oggetti si spostano uguale → parallax non funziona.
//
//   chronon3d_cli video ProofPanParallax --graph --start 0 --end 89 --fps 30 -o output/proofs/pan_parallax.mp4

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>

using namespace chronon3d;

static Composition proof_pan_parallax() {
    return composition({
        .name     = "ProofPanParallax",
        .width    = 1280,
        .height   = 720,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const float t = ctx.duration > 1
            ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1)
            : 0.0f;

        s.camera().set(camera_motion::pan(t, {
            .from_x = -120.0f,
            .to_x   =  120.0f,
            .z      = -1000.0f,
            .zoom   = 1000.0f
        }));

        s.rect("bg", {
            .size  = {1280.0f, 720.0f},
            .color = Color{0.06f, 0.06f, 0.07f, 1.0f},
            .pos   = {640.0f, 360.0f, 0.0f}
        });

        // Blue — FAR (z = +600)
        s.layer("far", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 600.0f});
            l.rect("card", {
                .size  = {200.0f, 200.0f},
                .color = Color{0.1f, 0.2f, 1.0f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        // Green — MID (z = 0)
        s.layer("mid", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 0.0f});
            l.rect("card", {
                .size  = {200.0f, 200.0f},
                .color = Color{0.1f, 0.85f, 0.2f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        // Red — NEAR (z = -300)
        s.layer("near", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, -300.0f});
            l.rect("card", {
                .size  = {200.0f, 200.0f},
                .color = Color{1.0f, 0.1f, 0.1f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ProofPanParallax", proof_pan_parallax)

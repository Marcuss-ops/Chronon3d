// ProofTiltParallax — verifica tilt verticale e parallasse verticale.
//
// Camera tilt da -10° a +10°.
// L'oggetto vicino (z=-300) si sposta verticalmente di più in screen space.
//
//   chronon3d_cli video ProofTiltParallax --graph --start 0 --end 89 --fps 30 -o output/proofs/tilt_parallax.mp4

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>

using namespace chronon3d;

static Composition proof_tilt_parallax() {
    return composition({
        .name     = "ProofTiltParallax",
        .width    = 1280,
        .height   = 720,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const float t  = ctx.duration > 1
            ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1)
            : 0.0f;
        const float st   = camera_motion::smoothstep(t);
        const float tilt = camera_motion::lerp(-10.0f, 10.0f, st);

        Camera2_5D cam;
        cam.enabled  = true;
        cam.zoom     = 1000.0f;
        cam.position = {0.0f, 0.0f, -1000.0f};
        cam.set_tilt(tilt);
        s.camera().set(cam);

        s.rect("bg", {
            .size  = {1280.0f, 720.0f},
            .color = Color{0.06f, 0.06f, 0.07f, 1.0f},
            .pos   = {640.0f, 360.0f, 0.0f}
        });

        // Blue — FAR (z = +400)
        s.layer("far", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 400.0f});
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

CHRONON_REGISTER_COMPOSITION("ProofTiltParallax", proof_tilt_parallax)

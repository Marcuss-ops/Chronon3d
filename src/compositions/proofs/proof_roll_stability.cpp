// ProofRollStability — verifica roll attorno al centro corretto.
//
// Camera roll da -15° a +15°. Il marker centrale deve restare a schermo-centro.
// I 4 corner marker gialli devono ruotare intorno al centro senza slittare.
//
//   chronon3d_cli video ProofRollStability --graph --start 0 --end 89 --fps 30 -o output/proofs/roll_stability.mp4

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>

using namespace chronon3d;

static Composition proof_roll_stability() {
    return composition({
        .name     = "ProofRollStability",
        .width    = 1280,
        .height   = 720,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const float t  = ctx.duration > 1
            ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1)
            : 0.0f;
        const float st   = camera_motion::smoothstep(t);
        const float roll = camera_motion::lerp(-15.0f, 15.0f, st);

        Camera2_5D cam;
        cam.enabled  = true;
        cam.zoom     = 1000.0f;
        cam.position = {0.0f, 0.0f, -1000.0f};
        cam.set_roll(roll);
        s.camera().set(cam);

        s.rect("bg", {
            .size  = {1280.0f, 720.0f},
            .color = Color{0.06f, 0.06f, 0.07f, 1.0f},
            .pos   = {640.0f, 360.0f, 0.0f}
        });

        // Center marker — white circle at world origin
        s.layer("center", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 0.0f});
            l.circle("dot", {
                .radius = 30.0f,
                .color  = Color{1.0f, 1.0f, 1.0f, 1.0f},
                .pos    = {0.0f, 0.0f, 0.0f}
            });
        });

        // 4 corner markers — yellow
        auto add_corner = [&s](const char* name, float x, float y) {
            s.layer(name, [x, y](LayerBuilder& l) {
                l.enable_3d().position({x, y, 0.0f});
                l.rect("marker", {
                    .size  = {40.0f, 40.0f},
                    .color = Color{1.0f, 0.9f, 0.1f, 1.0f},
                    .pos   = {0.0f, 0.0f, 0.0f}
                });
            });
        };

        add_corner("tl", -400.0f, -200.0f);
        add_corner("tr",  400.0f, -200.0f);
        add_corner("bl", -400.0f,  200.0f);
        add_corner("br",  400.0f,  200.0f);

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ProofRollStability", proof_roll_stability)

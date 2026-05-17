// ProofEdgeOnRotation — verifica rotazione Y di un oggetto da 0° a 90°.
//
// La card gialla deve stringersi fino a diventare una linea a 90°.
// Non deve allargarsi, invertire o sparire prima del dovuto.
//
//   chronon3d_cli render ProofEdgeOnRotation --graph --frames 0  -o output/proofs/edge_f000.png
//   chronon3d_cli render ProofEdgeOnRotation --graph --frames 45 -o output/proofs/edge_f045.png
//   chronon3d_cli render ProofEdgeOnRotation --graph --frames 89 -o output/proofs/edge_f089.png
//   chronon3d_cli video  ProofEdgeOnRotation --graph --start 0 --end 89 --fps 30 -o output/proofs/edge_on_rotation.mp4

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>

using namespace chronon3d;

static Composition proof_edge_on_rotation() {
    return composition({
        .name     = "ProofEdgeOnRotation",
        .width    = 1280,
        .height   = 720,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const float t = ctx.duration > 1
            ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1)
            : 0.0f;

        const float angle = camera_motion::lerp(0.0f, 90.0f, camera_motion::smoothstep(t));

        // Camera: static
        {
            Camera2_5D cam;
            cam.enabled  = true;
            cam.zoom     = 1000.0f;
            cam.position = {0.0f, 0.0f, -1000.0f};
            s.camera().set(cam);
        }

        s.rect("bg", {
            .size  = {1280.0f, 720.0f},
            .color = Color{0.06f, 0.06f, 0.07f, 1.0f},
            .pos   = {640.0f, 360.0f, 0.0f}
        });

        // Yellow card — Y-rotation from 0° to 90°
        s.layer("card", [angle](LayerBuilder& l) {
            l.enable_3d()
             .position({0.0f, 0.0f, 0.0f})
             .rotate({0.0f, angle, 0.0f});
            l.rect("face", {
                .size  = {300.0f, 180.0f},
                .color = Color{1.0f, 0.9f, 0.1f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ProofEdgeOnRotation", proof_edge_on_rotation)

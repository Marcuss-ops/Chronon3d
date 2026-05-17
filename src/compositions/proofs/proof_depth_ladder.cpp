// ProofDepthLadder — verifica z-depth e z-sorting.
//
// Tre card colorate a z diversi, staggerate in Y per renderle tutte visibili:
//   BLUE  z=+300  (lontana) — in alto
//   GREEN z=0     (media)   — al centro
//   RED   z=-300  (vicina)  — in basso
//
// Comportamento atteso:
//   - RED appare più grande di BLUE (prospettiva)
//   - Card più vicina occluda quella più lontana dove si sovrappongono
//   - Dolly-in da frame 45: scala apparente aumenta
//
//   chronon3d_cli render ProofDepthLadder --graph --frames 0  -o output/proofs/depth_f000.png
//   chronon3d_cli render ProofDepthLadder --graph --frames 89 -o output/proofs/depth_f089.png
//   chronon3d_cli video  ProofDepthLadder --graph --start 0 --end 89 --fps 30 -o output/proofs/depth_ladder.mp4

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>

using namespace chronon3d;

static Composition proof_depth_ladder() {
    return composition({
        .name     = "ProofDepthLadder",
        .width    = 1280,
        .height   = 720,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const float t = ctx.duration > 1
            ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1)
            : 0.0f;

        // Static first half, smooth dolly-in second half
        {
            Camera2_5D cam;
            cam.enabled  = true;
            cam.zoom     = 1000.0f;
            const float dt = camera_motion::smoothstep(
                camera_motion::clamp01((t - 0.5f) * 2.0f));
            cam.position = {0.0f, 0.0f, -1000.0f + dt * 200.0f};
            s.camera().set(cam);
        }

        // Dark 2D background
        s.rect("bg", {
            .size  = {1280.0f, 720.0f},
            .color = Color{0.06f, 0.06f, 0.07f, 1.0f},
            .pos   = {640.0f, 360.0f, 0.0f}
        });

        // Blue — FAR (z = +300), top
        s.layer("blue_far", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, -160.0f, 300.0f});
            l.rect("card", {
                .size  = {200.0f, 200.0f},
                .color = Color{0.1f, 0.2f, 1.0f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        // Green — MID (z = 0), center
        s.layer("green_mid", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 0.0f});
            l.rect("card", {
                .size  = {200.0f, 200.0f},
                .color = Color{0.1f, 0.85f, 0.2f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        // Red — NEAR (z = -300), bottom
        s.layer("red_near", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 160.0f, -300.0f});
            l.rect("card", {
                .size  = {200.0f, 200.0f},
                .color = Color{1.0f, 0.1f, 0.1f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ProofDepthLadder", proof_depth_ladder)

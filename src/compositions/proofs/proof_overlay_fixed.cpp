// ProofOverlayFixed — verifica che i layer 2D (senza enable_3d) restino fissi
// mentre la camera si muove.
//
// La card 3D blu si sposta con il pan.
// Il cerchio giallo (HUD 2D, no enable_3d) deve restare fermo.
//
//   chronon3d_cli video ProofOverlayFixed --graph --start 0 --end 89 --fps 30 -o output/proofs/overlay_fixed.mp4

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>

using namespace chronon3d;

static Composition proof_overlay_fixed() {
    return composition({
        .name     = "ProofOverlayFixed",
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

        // Dark 2D background
        s.rect("bg", {
            .size  = {1280.0f, 720.0f},
            .color = Color{0.06f, 0.06f, 0.07f, 1.0f},
            .pos   = {640.0f, 360.0f, 0.0f}
        });

        // 3D card — moves with camera
        s.layer("scene_card", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 0.0f});
            l.rect("card", {
                .size  = {300.0f, 200.0f},
                .color = Color{0.1f, 0.2f, 1.0f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        // 2D HUD marker — no enable_3d, ignores camera
        // Position in world-centered 2D coords (0,0 = screen center)
        s.layer("hud_dot", [](LayerBuilder& l) {
            l.position({-540.0f, -300.0f, 0.0f}); // top-left area
            l.circle("dot", {
                .radius = 24.0f,
                .color  = Color{1.0f, 0.9f, 0.1f, 1.0f},
                .pos    = {0.0f, 0.0f, 0.0f}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ProofOverlayFixed", proof_overlay_fixed)

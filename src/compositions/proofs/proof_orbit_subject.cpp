// ProofOrbitSubject — verifica orbit pan/tilt combinati.
//
// Camera orbita attorno al soggetto centrale con un angolo ±20°.
// Il soggetto (bianco) deve restare quasi centrato.
// Gli oggetti laterali (arancione e ciano) mostrano parallax orizzontale.
//
//   chronon3d_cli video ProofOrbitSubject --graph --start 0 --end 89 --fps 30 -o output/proofs/orbit_subject.mp4

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>

using namespace chronon3d;

static Composition proof_orbit_subject() {
    return composition({
        .name     = "ProofOrbitSubject",
        .width    = 1280,
        .height   = 720,
        .duration = 90
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const float t = ctx.duration > 1
            ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1)
            : 0.0f;

        s.camera().set(camera_motion::orbit(t, {
            .radius          = 80.0f,
            .z               = -1000.0f,
            .y               = 0.0f,
            .target          = {0.0f, 0.0f, 0.0f},
            .start_angle_deg = -20.0f,
            .end_angle_deg   =  20.0f,
            .zoom            = 1000.0f
        }));

        s.rect("bg", {
            .size  = {1280.0f, 720.0f},
            .color = Color{0.06f, 0.06f, 0.07f, 1.0f},
            .pos   = {640.0f, 360.0f, 0.0f}
        });

        // Far background plane
        s.layer("bg_plane", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 800.0f});
            l.rect("bg", {
                .size  = {1600.0f, 900.0f},
                .color = Color{0.1f, 0.1f, 0.2f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        // Left object — orange
        s.layer("left_obj", [](LayerBuilder& l) {
            l.enable_3d().position({-350.0f, 0.0f, 0.0f});
            l.rect("card", {
                .size  = {120.0f, 120.0f},
                .color = Color{1.0f, 0.5f, 0.1f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        // Central subject — white
        s.layer("subject", [](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 0.0f});
            l.rect("card", {
                .size  = {180.0f, 180.0f},
                .color = Color{1.0f, 1.0f, 1.0f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        // Right object — cyan
        s.layer("right_obj", [](LayerBuilder& l) {
            l.enable_3d().position({350.0f, 0.0f, 0.0f});
            l.rect("card", {
                .size  = {120.0f, 120.0f},
                .color = Color{0.1f, 0.9f, 0.9f, 1.0f},
                .pos   = {0.0f, 0.0f, 0.0f}
            });
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("ProofOrbitSubject", proof_orbit_subject)

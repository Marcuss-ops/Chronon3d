#include <chronon3d/chronon3d.hpp>
#include <fmt/format.h>
#include <cmath>

using namespace chronon3d;

/**
 * CameraTargetOrbitProof
 *
 * Two-node camera: camera is parented to an orbiting rig null and always
 * points at a "target" null that moves independently along a slow arc.
 * The combination of orbit + look-at produces continuous perspective
 * shifts not possible without camera_target().
 *
 * Based on the same camera rig setup as CameraOrbitProof (zoom=1000,
 * camera at {0,0,-1200} local to rig) — the diff is the lookAt added
 * by camera_target().
 */
static Composition CameraTargetOrbitProof() {
    return composition({
        .name     = "CameraTargetOrbitProof",
        .width    = 1280,
        .height   = 720,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const f32 t = ctx.frame / 120.0f; // 0..1 over full duration

        // 1. Target null — slow, non-symmetric drift so lookAt is clearly working
        const Vec3 target_pos = {
            std::sin(t * glm::two_pi<f32>() * 0.5f) * 300.0f,  // slow X oscillation
            0.0f,
            std::cos(t * glm::two_pi<f32>() * 0.7f) * 200.0f   // different frequency Z
        };

        s.null_layer("target", [&](LayerBuilder& l) {
            l.position(target_pos);
        });

        // 2. Camera orbit rig — full 360° over 120 frames (same as CameraOrbitProof)
        s.null_layer("cam_rig", [&](LayerBuilder& l) {
            l.position({0, 0, 0})
             .rotate({0, t * 360.0f, 0});
        });

        // 3. Two-node camera: orbit rig + look-at target
        s.enable_camera_2_5d()
         .camera_position({0, 0, -1200})
         .camera_zoom(1000.0f)
         .camera_parent("cam_rig")
         .camera_target("target");

        // 4. Background
        s.layer("bg", [](LayerBuilder& l) {
            l.rect("fill", {.size = {3000, 3000}, .color = Color{0.05f, 0.05f, 0.1f, 1.0f}});
        });

        // 5. Asymmetric colored pillars in 3D — distinct colors per axis
        //    so you can see the perspective rotation as camera orbits.
        struct Pillar { Vec3 pos; Color color; };
        const Pillar pillars[] = {
            {{  400, 0,    0}, Color{0.9f, 0.25f, 0.2f}},   // red   +X
            {{ -400, 0,    0}, Color{0.25f, 0.75f, 0.3f}},  // green -X
            {{    0, 0,  400}, Color{0.3f, 0.5f, 0.95f}},   // blue  +Z
            {{    0, 0, -400}, Color{0.95f, 0.8f, 0.1f}},   // yellow -Z
            {{  300, 0,  300}, Color{0.8f, 0.3f, 0.85f}},   // purple +X+Z
            {{ -300, 0, -300}, Color{0.2f, 0.85f, 0.85f}},  // cyan  -X-Z
        };

        for (const auto& p : pillars) {
            const auto name = fmt::format("pillar_{:.0f}_{:.0f}", p.pos.x, p.pos.z);
            s.layer(name, [p](LayerBuilder& l) {
                l.enable_3d()
                 .position(p.pos)
                 .rounded_rect("body", {
                     .size   = {100, 400},
                     .radius = 10,
                     .color  = p.color
                 });
            });
        }

        // 6. Target marker: red square at target world position
        s.layer("target_marker", [&](LayerBuilder& l) {
            l.enable_3d()
             .position(target_pos)
             .rect("dot", {.size = {50, 50}, .color = Color::red()});
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("CameraTargetOrbitProof", CameraTargetOrbitProof)

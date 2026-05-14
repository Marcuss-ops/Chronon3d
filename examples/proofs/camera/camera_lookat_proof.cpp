#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/animation/interpolate.hpp>
#include <fmt/format.h>
#include <cmath>

using namespace chronon3d;

/**
 * CameraLookAtProof
 * Demonstrates a 2-node camera rig with True Look-At.
 * The camera orbits and points at a moving target, creating 3D perspective distortion (keystoning).
 */
static Composition CameraLookAtProof() {
    return composition({
        .name = "CameraLookAtProof",
        .width = 1280,
        .height = 720,
        .duration = 120
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        const f32 t = ctx.frame / 60.0f;
        const f32 orbit_radius = 1200.0f;
        
        // 1. Moving Target (Point of Interest)
        const Vec3 poi = {
            std::sin(t * 0.5f) * 200.0f,
            std::cos(t * 0.7f) * 150.0f,
            0.0f
        };

        // Target Visual
        s.layer("poi_target", [&](LayerBuilder& l) {
            l.position(poi).rect("marker", {.size = {20, 20}, .color = Color::red()});
        });

        // 2. Camera Rig
        const Vec3 cam_pos = {
            poi.x + std::sin(t) * orbit_radius,
            poi.y + 400.0f,
            poi.z + std::cos(t) * orbit_radius
        };

        s.camera_2_5d({
            .enabled = true,
            .position = cam_pos,
            .point_of_interest = poi,
            .projection_mode = Camera2_5DProjectionMode::Fov,
            .fov_deg = 45.0f
        });

        // 3. World Grid (3D Layers)
        for (int x = -2; x <= 2; ++x) {
            for (int z = -2; z <= 2; ++z) {
                s.layer(fmt::format("grid_{}_{}", x, z), [&](LayerBuilder& l) {
                    l.position({x * 400.0f, 0.0f, z * 400.0f})
                     .rotate({90, 0, 0})
                     .enable_3d(true)
                     .rect("tile", {
                         .size = {150, 150}, 
                         .color = Color(0.2f + (x+2)*0.15f, 0.5f, 0.8f - (z+2)*0.15f)
                     });
                });
            }
        }

        // 4. Floating Vertical Cards
        s.layer("v_card_1", [&](LayerBuilder& l) {
            l.position({300, 200, 300})
             .rotate({0, 45, 0})
             .enable_3d(true)
             .rect("card", {.size = {200, 300}, .color = Color::white().with_alpha(0.8f)});
        });

        s.layer("v_card_2", [&](LayerBuilder& l) {
            l.position({-300, 200, -300})
             .rotate({0, -45, 0})
             .enable_3d(true)
             .rect("card", {.size = {200, 300}, .color = Color::white().with_alpha(0.8f)});
        });

        return s.build();
    });
}

CHRONON_REGISTER_COMPOSITION("CameraLookAtProof", CameraLookAtProof)

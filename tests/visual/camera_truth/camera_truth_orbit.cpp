// ==============================================================================
// tests/visual/camera_truth/camera_truth_orbit.cpp
//
// CameraTruthOrbit — 3D orbit camera projection truth scene.
//
// Same 4 cards as CameraTruthTest but with OrbitMotion via CameraDescriptor.
// Camera orbits around origin (yaw 0→90° at radius 1000) so the render path
// exercises the compiled orbit evaluation (`eval_orbit_motion`) end-to-end.
// ==============================================================================

#include "camera_truth_orbit.hpp"

#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_v1/camera_descriptor.hpp>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include <cmath>

using namespace chronon3d;

namespace chronon3d::test {

Composition make_camera_truth_orbit() {
    // ── Build the CameraDescriptor with OrbitMotion ─────────────────
    camera_v1::CameraDescriptor desc;
    desc.id = "camera_truth_orbit";
    desc.base.position = Vec3{0.0f, 0.0f, -1000.0f};
    desc.base.rotation = Vec3{0.0f, 0.0f, 0.0f};
    desc.base.projection = camera_v1::ZoomProjection{AnimatedValue<float>{1000.0f}};
    desc.base.point_of_interest_enabled = true;
    desc.base.point_of_interest = Vec3{0.0f, 0.0f, 0.0f};

    camera_v1::OrbitMotion orbit;
    orbit.target.set(Vec3{0.0f, 0.0f, 0.0f});
    orbit.yaw.key(Frame{0}, 0.0f).key(Frame{60}, 90.0f);
    orbit.pitch.set(0.0f);
    orbit.radius.set(1000.0f);
    orbit.track.set(Vec3{0.0f, 0.0f, 0.0f});
    orbit.dolly.set(0.0f);
    orbit.roll.set(0.0f);

    desc.source = orbit;
    desc.orientation = camera_v1::LookAtPoint{Vec3{0.0f, 0.0f, 0.0f}};

    // ── Composition ─────────────────────────────────────────────────
    auto comp = composition(
        {.name = "CameraTruthOrbit", .width = 960, .height = 540, .duration = 61},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            const Frame f = ctx.frame();

            // ── Background grid (2D, screen-space reference) ──────────
            s.layer("bg", [](LayerBuilder& l) {
                l.grid_background("grid", GridBackgroundParams{
                    .size = {960.0f, 540.0f},
                    .bg_color = {0.02f, 0.02f, 0.05f, 1.0f},
                    .grid_color = {0.28f, 0.48f, 0.98f, 0.08f},
                    .spacing = 60.0f,
                    .minor_thickness = 1.0f,
                    .major_thickness = 2.0f,
                    .major_every = 4,
                    .centered = true
                });
            });

            // ── Four 3D cards at different Z depths ───────────────────
            // Same layout as CameraTruthTest
            s.layer("card_behind", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, -1200.0f});
                l.rect("rect", {
                    .size = {120.0f, 80.0f},
                    .color = Color{0.85f, 0.10f, 0.10f, 0.6f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{0.85f, 0.10f, 0.10f, 0.6f})
                });
            });

            s.layer("card_near", [](LayerBuilder& l) {
                l.enable_3d().position({-300.0f, 0.0f, 0.0f});
                l.rect("rect", {
                    .size = {100.0f, 65.0f},
                    .color = Color{1.0f, 0.15f, 0.15f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{1.0f, 0.15f, 0.15f, 1.0f})
                });
            });

            s.layer("card_mid", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 500.0f});
                l.rect("rect", {
                    .size = {100.0f, 65.0f},
                    .color = Color{0.15f, 0.9f, 0.20f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{0.15f, 0.9f, 0.20f, 1.0f})
                });
            });

            s.layer("card_far", [](LayerBuilder& l) {
                l.enable_3d().position({300.0f, 0.0f, 1000.0f});
                l.rect("rect", {
                    .size = {100.0f, 65.0f},
                    .color = Color{0.15f, 0.35f, 1.0f, 1.0f},
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = FillStyle::solid(Color{0.15f, 0.35f, 1.0f, 1.0f})
                });
            });

            // ── Crosshair at origin (3D, at z=0) ─────────────────────
            s.layer("origin_cross", [](LayerBuilder& l) {
                l.enable_3d().position({0.0f, 0.0f, 0.0f});
                l.rect("h", {.size = {50.0f, 3.0f}, .color = Color::white(),
                             .pos = {0.0f, 0.0f, 0.0f},
                             .fill = FillStyle::solid(Color::white())});
                l.rect("v", {.size = {3.0f, 50.0f}, .color = Color::white(),
                             .pos = {0.0f, 0.0f, 0.0f},
                             .fill = FillStyle::solid(Color::white())});
            });

            // ── HUD: segment label ───────────────────────────────────
            s.layer("hud", [f](LayerBuilder& l) {
                l.pin_to(chronon3d::Anchor::TopLeft, 12.0f);
                l.opacity(0.85f);
                // Build the label string outside the designated-initializer
                // so the `+` concatenation has a clean LHS/RHS (the previous
                // inline form had a dangling `+,` at line 127 which broke
                // the build).  `f` is captured by value above.
                const int yaw_deg_int = static_cast<int>(std::round(
                    static_cast<float>(static_cast<int>(f)) / 60.0f * 90.0f));
                const std::string hud_label = std::string("ORBIT  |  yaw=")
                    + std::to_string(yaw_deg_int) + "\u00B0";
                l.text("hud_label", chronon3d::TextSpec{
                    .content    = {.value = hud_label},
                    .placement = TextPlacement{TextPlacementKind::Absolute, {0.0f, 0.0f}},
                    .font = {.font_family = "Inter", .font_weight = 700, .font_size = 18.0f},
                    .layout = {.box = {500.0f, 24.0f}, .align = chronon3d::TextAlign::Left,
                               .line_height = 1.20f, .tracking = 2.0f},
                    .appearance = {.color = chronon3d::Color{1.0f, 0.70f, 0.30f, 1.0f}},
                });
            });

            // ── Diagnostic: compute orbit camera position manually and
            //    log world→screen projection for each card.  This mirrors
            //    eval_orbit_motion() in camera_program.cpp.
            //    orbit_position = target + world_forward * radius
            //    where world_forward = (cos(pitch)*sin(yaw), sin(pitch), cos(pitch)*cos(yaw))

            const float t = static_cast<float>(static_cast<int>(f)) / 60.0f;  // 0→1 across frames
            const float yaw_deg = t * 90.0f;
            const float yaw_rad = glm::radians(yaw_deg);
            const float pitch_rad = 0.0f;
            const float radius = 1000.0f;
            const Vec3 target{0.0f, 0.0f, 0.0f};

            const Vec3 world_forward{
                std::cos(pitch_rad) * std::sin(yaw_rad),
                std::sin(pitch_rad),
                std::cos(pitch_rad) * std::cos(yaw_rad)
            };
            const Vec3 campos = target + world_forward * radius;

            spdlog::info("[CameraTruthOrbit] frame={} yaw={:.1f}\u00B0 cam_pos=({:.0f},{:.0f},{:.0f})",
                         static_cast<int>(f), yaw_deg,
                         campos.x, campos.y, campos.z);

            auto log_card = [f, &campos](const char* name, Vec3 world_pos) {
                // Simplified screen projection: use zoom=1000, project
                // along the camera→target axis.  For small yaw angles
                // this approximates the real projection; for larger yaw
                // the screen_x drift demonstrates orbital parallax.
                float zoom = 1000.0f;
                // Depth = distance from camera to world_pos along view axis
                // For orbit, view direction is normalize(target - campos)
                Vec3 to_target = Vec3{0.0f, 0.0f, 0.0f} - campos;
                float to_target_len = glm::length(to_target);
                Vec3 view_dir = (to_target_len > 0.001f) ? to_target / to_target_len : Vec3{0.0f, 0.0f, 1.0f};

                Vec3 offset = world_pos - campos;
                float depth = glm::dot(offset, view_dir);
                float scale = (depth > 0.001f) ? zoom / depth : 0.0f;

                // Camera-local right/up for screen projection
                Vec3 world_up{0.0f, 1.0f, 0.0f};
                Vec3 right = glm::normalize(glm::cross(view_dir, world_up));
                Vec3 up = glm::cross(right, view_dir);

                float screen_x = 480.0f + glm::dot(offset, right) * scale;
                float screen_y = 270.0f - glm::dot(offset, up) * scale;

                spdlog::info("[CameraTruthOrbit] frame={} layer={:10s} "
                             "world=({:6.0f},{:6.0f},{:6.0f}) "
                             "depth={:7.0f} scale={:.3f} "
                             "screen=({:7.1f},{:7.1f}) visible={}",
                             static_cast<int>(f), name,
                             world_pos.x, world_pos.y, world_pos.z,
                             depth, scale,
                             screen_x, screen_y,
                             (depth > 0.0f) ? "true" : "false");
            };

            log_card("behind",     Vec3{0.0f, 0.0f, -1200.0f});
            log_card("near(red)",  Vec3{-300.0f, 0.0f, 0.0f});
            log_card("mid(green)", Vec3{0.0f, 0.0f, 500.0f});
            log_card("far(blue)",  Vec3{300.0f, 0.0f, 1000.0f});

            return s.build();
        }
    );

    comp.default_camera_descriptor(std::move(desc));
    return comp;
}

} // namespace chronon3d::test

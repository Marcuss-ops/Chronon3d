#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <chronon3d/scene/camera/camera_projection.hpp>
#include <chronon3d/scene/camera/camera_shot_validator.hpp>
#include <chronon3d/scene/camera/camera_framing.hpp>
#include <chronon3d/scene/camera/camera_path_sampler.hpp>
#include <chronon3d/scene/camera/camera_debug_overlay.hpp>
#include <chronon3d/scene/camera/camera_rig_presets.hpp>
#include <chronon3d/scene/camera/camera_shot_profile.hpp>
#include <chronon3d/math/color.hpp>
#include <fstream>

namespace chronon3d::content::two_point_five_d {

namespace {

constexpr f32 W = 1920.0f;
constexpr f32 H = 1080.0f;

} // namespace

Composition parallax_simple() {
    return composition({.name = "ParallaxSimple", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        const f32 cam_x = -p * 400.0f;

        s.camera().set(camera_motion::dolly(p, {.from_z = -900, .to_z = -720, .zoom = 800}));

        s.layer("far_bg", [&](auto& l) { 
            l.enable_3d().position({cam_x * 0.2f, 0.0f, 300.0f});
            l.rect("far", {.size = {W * 2, H}, .color = {0.02f, 0.03f, 0.08f, 1}});
        });

        s.layer("mid_bg", [&](auto& l) {
            l.enable_3d().position({cam_x * 0.5f, 0.0f, 100.0f});
            l.rect("mid", {.size = {W * 1.5f, H}, .color = {0.04f, 0.06f, 0.12f, 1}});
        });

        for (int i = 0; i < 5; ++i) {
            s.layer("mid_elem_" + std::to_string(i), [&](auto& l) {
                l.enable_3d().position({i * 500.0f - 1000.0f + cam_x * 0.5f, static_cast<f32>(i % 3 - 1) * 200.0f, 100.0f});
                l.circle("c", {.radius = 20.0f + i * 10.0f, .color = {0.25f, 0.52f, 1, 0.15f}});
            });
        }

        s.layer("fg", [&](auto& l) {
            l.enable_3d().position({cam_x * 1.0f, 0.0f, -200.0f});
            l.rect("fg_rect", {.size = {W, 300}, .color = {0.06f, 0.08f, 0.14f, 1}, .pos = {0, 400, 0}});
        });

        for (int i = 0; i < 3; ++i) {
            s.layer("fg_box_" + std::to_string(i), [&](auto& l) {
                l.enable_3d().position({i * 600.0f - 600.0f + cam_x * 1.0f, -50.0f - i * 80.0f, -200.0f});
                l.rounded_rect("box", {.size = {180, 140}, .radius = 12, .color = {0.25f, 0.52f, 1, 0.4f}});
            });
        }

        s.layer("label", [&](auto& l) {
            l.opacity(0.7f).pin_to(Anchor::BottomLeft, 40.0f);
            l.text("txt", {
                .text = "Parallax Demo  |  Far(0.2x)  Mid(0.5x)  FG(1.0x)",
                .size = {W*0.5f, 30},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_family = "Inter",
                .font_weight = 800,
                .font_size = 22.0f,
                .color = Color{0.6f, 0.7f, 0.9f, 1},
                .align = TextAlign::Left,
                .line_height = 1.2f,
                .tracking = 1.5f
            });
        });

        return s.build();
    });
}

Composition depth_scene() {
    return composition({.name = "DepthScene", .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        const f32 cam_z = camera_motion::lerp(-800.0f, -500.0f, camera_motion::smoothstep(p));

        s.camera().set(camera_motion::push_in_tilt(p, {.from_z = -800, .to_z = -500, .from_tilt = -4, .to_tilt = 4, .zoom = 800}));
        s.ambient_light({1, 1, 1, 1}, 0.2f);
        s.directional_light({-0.5f, 1, -0.5f}, {1, 1, 1, 1}, 0.8f);

        auto depth_l = [&](const char* name, f32 z, auto draw_func) {
            s.layer(name, [&](auto& l) { l.enable_3d().position({0, 0, z}); draw_func(l); });
        };

        depth_l("far_layer", 200, [](auto& l) { l.rect("far", {.size = {1600, 900}, .color = {0.05f, 0.08f, 0.20f, 1}}); });
        depth_l("mid_layer", 0, [](auto& l) {
            l.rounded_rect("mid", {.size = {800, 500}, .radius = 24, .color = {0.12f, 0.15f, 0.25f, 1}});
            l.rect("inner", {.size = {400, 200}, .color = {0.25f, 0.52f, 1, 0.12f}});
        });
        depth_l("near_layer", -150, [](auto& l) {
            l.position({0, -100, -150});
            l.rounded_rect("near", {.size = {300, 80}, .radius = 16, .color = {0.25f, 0.52f, 1, 0.6f}});
        });

        for (int i = 0; i < 4; ++i) {
            const f32 angle = i * 1.5708f;
            s.layer("pillar_" + std::to_string(i), [&](auto& l) {
                l.enable_3d().position({std::cos(angle) * 500.0f, 0, std::sin(angle) * 100.0f}).casts_shadows(true).accepts_shadows(true);
                l.fake_box3d("box", {.size = {40, 300}, .depth = 40, .color = {0.2f, 0.3f, 0.5f, 1}, .contact_shadow = true, .floor_y = -250});
            });
        }

        s.layer("floor", [](auto& l) {
            l.enable_3d().position({0, -250, 0}).accepts_shadows(true);
            l.rect("floor", {.size = {2000, 2000}, .color = {0.03f, 0.04f, 0.06f, 1}});
        });

        s.layer("info", [&](auto& l) {
            l.pin_to(Anchor::BottomRight, 40.0f);
            l.text("txt", {
                .text = "Depth: " + std::to_string(static_cast<i32>(cam_z)),
                .size = {200, 30},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_family = "Inter",
                .font_weight = 800,
                .font_size = 20.0f,
                .color = Color{0.6f, 0.7f, 0.9f, 0.5f},
                .align = TextAlign::Right,
                .line_height = 1.2f,
                .tracking = 0.0f
            });
        });

        return s.build();
    });
}

Composition card_flip() {
    return composition({.name = "CardFlip", .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        const f32 angle_y = p * 360.0f;
        const f32 scale_x = std::cos(angle_y * 0.0174533f);

        s.camera().set(camera_motion::orbit_small(p, 800.0f));
        s.layer("bg", [](auto& l) { l.fill({0.008f, 0.012f, 0.025f, 1}); });

        s.layer("card", [&](auto& l) {
            l.enable_3d().rotate({0, angle_y, 0}).casts_shadows(true).accepts_shadows(true);
            if (scale_x > 0) {
                l.rounded_rect("front", {.size = {400, 560}, .radius = 20, .color = {0.12f, 0.15f, 0.25f, 1}});
                l.rect("header", {.size = {360, 80}, .color = {0.25f, 0.52f, 1, 0.3f}, .pos = {0, -200, 0.1f}});
                l.text("label", {
                    .text = "FLIP",
                    .size = {320, 80},
                    .pos = {0, 0, 0.2f},
                    .font_family = "Inter",
                    .font_weight = 800,
                    .font_size = 56.0f,
                    .color = Color{1.0f, 1.0f, 1.0f, 1.0f},
                    .align = TextAlign::Center,
                    .line_height = 1.2f,
                    .tracking = 8.0f
                });
            } else {
                l.rounded_rect("back", {.size = {400, 560}, .radius = 20, .color = {0.08f, 0.10f, 0.18f, 1}});
                l.text("label", {
                    .text = "2.5D",
                    .size = {320, 80},
                    .pos = {0, 0, 0.2f},
                    .font_family = "Inter",
                    .font_weight = 800,
                    .font_size = 56.0f,
                    .color = Color{0.4f, 0.6f, 0.9f, 1},
                    .align = TextAlign::Center,
                    .line_height = 1.2f,
                    .tracking = 8.0f
                });
            }
        });

        s.layer("floor_shadow", [&](auto& l) {
            l.enable_3d().position({0, -350, 0});
            l.rect("shadow", {.size = {400 * (std::abs(scale_x) * 0.8f + 0.2f), 60}, .color = {0, 0, 0, 0.2f * (1 - std::abs(scale_x) * 0.3f)}});
        });

        return s.build();
    });
}

Composition parallax_text() {
    return composition({.name = "ParallaxText", .duration = 120}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        const f32 move = -p * 300.0f;

        s.camera().set(camera_motion::parallax_sweep(p, 120, -900, 800));

        for (int i = 0; i < 20; ++i) {
            s.layer("star_" + std::to_string(i), [&](auto& l) {
                l.enable_3d().position({(i * 200.0f - 2000.0f) + move * 0.1f, std::sin(i * 1.7f) * 400.0f, 400})
                 .opacity(0.3f + 0.2f * std::sin(p * 3.0f + i));
                l.circle("dot", {.radius = 3, .color = {1, 1, 1, 1}});
            });
        }

        for (int i = 0; i < 6; ++i) {
            s.layer("mid_" + std::to_string(i), [&](auto& l) {
                l.enable_3d().position({(i * 400.0f - 1000.0f) + move * 0.4f, 0, 100});
                l.rounded_rect("shape", {.size = {120, 80}, .radius = 10, .color = {0.25f, 0.52f, 1, 0.12f}});
            });
        }

        s.layer("parallax_title", [&](auto& l) {
            l.opacity(std::min(1.0f, p * 3.0f)).pin_to(Anchor::Center);
            l.text("title", {
                .text = "2.5D PARALLAX",
                .size = {W * 0.85f, 120},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_family = "Inter",
                .font_weight = 800,
                .font_size = 80.0f,
                .color = Color{1.0f, 1.0f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .line_height = 1.2f,
                .tracking = 10.0f
            });
        });

        s.layer("parallax_sub", [&](auto& l) {
            l.opacity(std::min(1.0f, (p - 0.1f) * 4.0f)).pin_to(Anchor::Center).position({0, 90, 0});
            l.text("sub", {
                .text = "Multi-layer depth with motion",
                .size = {W * 0.6f, 50},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_family = "Inter",
                .font_weight = 800,
                .font_size = 30.0f,
                .color = Color{0.7f, 0.7f, 0.85f, 1},
                .align = TextAlign::Center,
                .line_height = 1.2f,
                .tracking = 2.0f
            });
        });

        return s.build();
    });
}



Composition camera_rig_orbit_reveal_test() {
    return composition({.name = "CameraRigOrbitRevealTest", .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.null_layer("camera_target", [](NullBuilder& n) {
            n.position({0.0f, 0.0f, 0.0f});
        });

        // Add 3 cards at different depths
        s.layer("card_back", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({0.0f, 0.0f, 200.0f});
            l.rounded_rect("back_rect", {
                .size = {400.0f, 250.0f},
                .radius = 16.0f,
                .color = Color{0.15f, 0.18f, 0.35f, 0.85f},
                .stroke = { .enabled = true, .color = Color{0.0f, 0.9f, 1.0f, 0.25f}, .width = 1.0f }
            });
            l.text("back_label", {
                .text = "BACK",
                .pos = {20.0f, 30.0f, 0.0f},
                .font_size = 14.0f,
                .color = Color{1.0f, 1.0f, 1.0f, 0.40f}
            });
            l.drop_shadow(Vec2{0.0f, 4.0f}, Color{0.0f, 0.0f, 0.0f, 0.15f}, 8.0f);
        });

        s.layer("card_mid", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({0.0f, 0.0f, 0.0f});
            l.rounded_rect("mid_rect", {
                .size = {350.0f, 220.0f},
                .radius = 16.0f,
                .color = Color{0.25f, 0.52f, 1.0f, 0.92f},
                .stroke = { .enabled = true, .color = Color{0.0f, 0.9f, 1.0f, 0.35f}, .width = 1.25f }
            });
            l.text("mid_label", {
                .text = "MID",
                .pos = {20.0f, 30.0f, 0.0f},
                .font_size = 14.0f,
                .color = Color{1.0f, 1.0f, 1.0f, 0.60f}
            });
            l.drop_shadow(Vec2{0.0f, 8.0f}, Color{0.0f, 0.0f, 0.0f, 0.25f}, 12.0f);
        });

        s.layer("card_front", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({0.0f, 0.0f, -200.0f});
            l.rounded_rect("front_rect", {
                .size = {300.0f, 190.0f},
                .radius = 16.0f,
                .color = Color{0.99f, 0.44f, 0.82f, 1.0f},
                .stroke = { .enabled = true, .color = Color{0.0f, 0.9f, 1.0f, 0.5f}, .width = 1.5f }
            });
            l.text("front_label", {
                .text = "FRONT",
                .pos = {20.0f, 30.0f, 0.0f},
                .font_size = 14.0f,
                .color = Color{1.0f, 1.0f, 1.0f, 0.80f}
            });
            l.drop_shadow(Vec2{0.0f, 14.0f}, Color{0.0f, 0.0f, 0.0f, 0.4f}, 20.0f);
            l.glow(15.0f, 0.4f, Color{0.0f, 0.9f, 1.0f, 0.5f});
        });

        // Debug background grid
        s.layer("bg_grid", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({0.0f, 0.0f, 500.0f});
            l.grid_background("grid", GridBackgroundParams{
                .size = {1920.0f, 1080.0f},
                .bg_color = {0.02f, 0.02f, 0.05f, 1.0f},
                .grid_color = {0.28f, 0.48f, 0.98f, 0.04f},
                .spacing = 100.0f,
                .minor_thickness = 1.0f,
                .major_thickness = 2.0f,
                .major_every = 4,
                .centered = true
            });
        });

        // Fullscreen vignette overlay
        s.layer("vignette", [](LayerBuilder& l) {
            l.rect("vignette_rect", {
                .size = {1920.0f, 1080.0f},
                .color = Color::white(),
                .fill = Fill::radial(
                    {0.5f, 0.5f},
                    0.90f,
                    {
                        {0.0f, Color{0.0f, 0.0f, 0.0f, 0.0f}},
                        {0.6f, Color{0.0f, 0.0f, 0.0f, 0.0f}},
                        {1.0f, Color{0.0f, 0.0f, 0.0f, 0.55f}}
                    }
                )
            });
        });

        Scene scene = s.build();

        // 1. Resolve hierarchy
        scene.resolve_hierarchy(ctx.frame);

        // 2. Build SceneTransformRegistry to resolve parent/target nulls
        SceneTransformRegistry registry;
        for (const auto& layer : scene.layers()) {
            Transform3D t3d;
            t3d.position = layer.transform.position;
            t3d.rotation = glm::degrees(glm::eulerAngles(layer.transform.rotation));
            t3d.scale = layer.transform.scale;
            t3d.anchor = layer.transform.anchor;
            t3d.parent_name = std::string(layer.parent_name);
            t3d.inherits_position = true;
            t3d.inherits_rotation = true;
            t3d.inherits_scale = true;
            registry.add_node(std::string(layer.name), t3d, layer.kind != LayerKind::Null);
        }
        auto resolved = registry.resolve_all();

        // 3. Set up CameraShotProfile with orbit reveal preset
        CameraShotProfile shot;
        shot.rig = camera_rig_presets::orbit_reveal("camera_target");
        shot.validator
            .register_layer_size("card_front", {300.0f, 190.0f})
            .register_layer_size("card_mid", {350.0f, 220.0f})
            .register_layer_size("card_back", {400.0f, 250.0f})
            .require_target_centered("camera_target", 3.0f)
            .require_visible("card_front", 0.95f)
            .require_visible("card_mid", 0.75f)
            .require_visible("card_back", 0.50f)
            .require_inside_safe_area("card_front", 0.08f)
            .require_depth_order({"card_front", "card_mid", "card_back"});
        shot.auto_fit = true;

        // Evaluate baseline camera
        Camera2_5D cam = shot.rig.evaluate(ctx.frame, &resolved);

        if (shot.auto_fit) {
            cam = fit_camera_to_layers(
                cam,
                {"card_front", "card_mid", "card_back"},
                resolved,
                {1920.0f, 1080.0f},
                shot.framing
            );
        }

        // Apply updated camera back to the scene
        scene.set_camera_2_5d(cam);

        // 4. Validate shot
        auto report = shot.validator.validate(cam, resolved, {1920.0f, 1080.0f});
        report.composition_name = "CameraRigOrbitRevealTest";
        report.frame = static_cast<int>(ctx.frame);

        // 5. Emit JSON reports at frame 90
        if (ctx.frame == 90 && shot.emit_report) {
            // Find layer ratios
            float front_ratio = 0.0f;
            float mid_ratio = 0.0f;
            float back_ratio = 0.0f;
            for (const auto& lr : report.layers) {
                if (lr.name == "card_front") front_ratio = lr.bounds.visible_ratio;
                else if (lr.name == "card_mid") mid_ratio = lr.bounds.visible_ratio;
                else if (lr.name == "card_back") back_ratio = lr.bounds.visible_ratio;
            }

            bool depth_order_valid = true;
            bool safe_area_valid = true;
            for (const auto& f : report.failures) {
                if (f.find("Depth order") != std::string::npos) depth_order_valid = false;
                if (f.find("safe area") != std::string::npos || f.find("outside safe") != std::string::npos) safe_area_valid = false;
            }

            std::ofstream out_shot("camera_shot_report_0090.json");
            if (out_shot) {
                out_shot << "{\n"
                         << "  \"composition\": \"" << report.composition_name << "\",\n"
                         << "  \"frame\": " << report.frame << ",\n"
                         << "  \"camera\": {\n"
                         << "    \"mode\": \"TwoNode\",\n"
                         << "    \"target\": \"camera_target\",\n"
                         << "    \"orbit_radius\": " << shot.rig.orbit_radius.evaluate(ctx.frame) << ",\n"
                         << "    \"fov_deg\": " << cam.fov_deg << "\n"
                         << "  },\n"
                         << "  \"validation\": {\n"
                         << "    \"passed\": " << (report.passed ? "true" : "false") << ",\n"
                         << "    \"target_center_error_px\": " << report.target_center_error_px << ",\n"
                         << "    \"front_visible_ratio\": " << front_ratio << ",\n"
                         << "    \"mid_visible_ratio\": " << mid_ratio << ",\n"
                         << "    \"back_visible_ratio\": " << back_ratio << ",\n"
                         << "    \"depth_order_valid\": " << (depth_order_valid ? "true" : "false") << ",\n"
                         << "    \"safe_area_valid\": " << (safe_area_valid ? "true" : "false") << "\n"
                         << "  }\n"
                         << "}\n";
            }

            auto path_report = sample_camera_path(shot.rig, resolved, {1920.0f, 1080.0f}, 0, 90, 1);
            std::ofstream out_path("camera_path_report.json");
            if (out_path) {
                out_path << "{\n"
                         << "  \"samples_count\": " << path_report.samples.size() << ",\n"
                         << "  \"max_target_center_error_px\": " << path_report.max_target_center_error_px << ",\n"
                         << "  \"max_velocity_jump\": " << path_report.max_velocity_jump << ",\n"
                         << "  \"max_acceleration_jump\": " << path_report.max_acceleration_jump << ",\n"
                         << "  \"passed\": " << (path_report.passed ? "true" : "false") << "\n"
                         << "}\n";
            }
        }

        // 6. Draw debug overlay nodes onto the scene
        SceneBuilder s_overlay(ctx);
        add_camera_debug_overlay(s_overlay, report);
        Scene overlay_scene = s_overlay.build();
        for (auto& node : overlay_scene.nodes()) {
            scene.add_node(std::move(node));
        }

        return scene;
    });
}

void register_all() {}

} // namespace chronon3d::content::two_point_five_d

CHRONON_REGISTER_COMPOSITION("ParallaxSimple", chronon3d::content::two_point_five_d::parallax_simple)
CHRONON_REGISTER_COMPOSITION("DepthScene",     chronon3d::content::two_point_five_d::depth_scene)
CHRONON_REGISTER_COMPOSITION("CardFlip",       chronon3d::content::two_point_five_d::card_flip)
CHRONON_REGISTER_COMPOSITION("ParallaxText",   chronon3d::content::two_point_five_d::parallax_text)
CHRONON_REGISTER_COMPOSITION("CameraRigOrbitRevealTest", chronon3d::content::two_point_five_d::camera_rig_orbit_reveal_test)

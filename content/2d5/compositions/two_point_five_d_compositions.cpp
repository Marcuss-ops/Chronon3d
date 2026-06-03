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

// ─────────────────────────────────────────────────────────────────────────────
// CAMERA VALIDATION & REGRESSION TESTING SUITE
// ─────────────────────────────────────────────────────────────────────────────

// Helper function to generate and append validation debug overlay and write reports
Scene camera_test_orchestrator(
    const FrameContext& ctx, 
    SceneBuilder& s, 
    const CameraShotProfile& shot, 
    const std::string& comp_name,
    const std::vector<std::string>& fit_layers,
    const std::vector<int>& report_frames = {0, 30, 45, 60, 90}
) {
    Scene scene = s.build();
    scene.resolve_hierarchy(ctx.frame);

    // Build transform registry for validation/framing
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

    // Evaluate camera rig from profile
    Camera2_5D cam = shot.rig.evaluate(ctx.frame, &resolved);

    // Apply auto fit if requested
    if (shot.auto_fit && !fit_layers.empty()) {
        cam = fit_camera_to_layers(
            cam,
            fit_layers,
            resolved,
            {static_cast<f32>(ctx.width), static_cast<f32>(ctx.height)},
            shot.framing
        );
    }
    scene.set_camera_2_5d(cam);

    // Perform validation
    auto report = shot.validator.validate(cam, resolved, {static_cast<f32>(ctx.width), static_cast<f32>(ctx.height)});
    report.composition_name = comp_name;
    report.frame = static_cast<int>(ctx.frame);

    // Determine if we should emit report for current frame
    bool should_report = false;
    for (int rf : report_frames) {
        if (static_cast<int>(ctx.frame) == rf) {
            should_report = true;
            break;
        }
    }

    if (should_report && shot.emit_report) {
        // Collect metrics
        float front_ratio = 0.0f;
        float mid_ratio = 0.0f;
        float back_ratio = 0.0f;
        for (const auto& lr : report.layers) {
            if (lr.name == "card_front" || lr.name == "card_a") front_ratio = lr.bounds.visible_ratio;
            else if (lr.name == "card_mid") mid_ratio = lr.bounds.visible_ratio;
            else if (lr.name == "card_back" || lr.name == "card_b") back_ratio = lr.bounds.visible_ratio;
        }

        bool depth_order_valid = true;
        bool safe_area_valid = true;
        for (const auto& f : report.failures) {
            if (f.find("Depth order") != std::string::npos) depth_order_valid = false;
            if (f.find("safe area") != std::string::npos || f.find("outside safe") != std::string::npos) safe_area_valid = false;
        }

        // Always write two files: a generic one for frame 90 or specific ones for progress frames
        std::string path = comp_name + "_report.json";
        if (ctx.frame != 90) {
            // zero-pad output paths for progress matching
            std::string frame_str = std::to_string(ctx.frame);
            while (frame_str.length() < 4) frame_str = "0" + frame_str;
            path = comp_name + "_report_" + frame_str + ".json";
        }

        std::ofstream out(path);
        if (out) {
            out << "{\n"
                << "  \"passed\": " << (report.passed ? "true" : "false") << ",\n"
                << "  \"target_center_error_px\": " << report.target_center_error_px << ",\n"
                << "  \"front_visible_ratio\": " << front_ratio << ",\n"
                << "  \"mid_visible_ratio\": " << mid_ratio << ",\n"
                << "  \"back_visible_ratio\": " << back_ratio << ",\n"
                << "  \"safe_area_valid\": " << (safe_area_valid ? "true" : "false") << ",\n"
                << "  \"depth_order_valid\": " << (depth_order_valid ? "true" : "false") << "\n"
                << "}\n";
        }
    }

    // Append Overlay elements onto final output Scene nodes list
    SceneBuilder s_overlay(ctx);
    add_camera_debug_overlay(s_overlay, report);
    Scene overlay_scene = s_overlay.build();
    for (auto& node : overlay_scene.nodes()) {
        scene.add_node(std::move(node));
    }

    return scene;
}

// 1. CameraOrbitTargetLockTest (Main camera rig orbiting from yaw -25 to 25 degrees)
Composition camera_orbit_target_lock_test() {
    return composition({.name = "CameraOrbitTargetLockTest", .duration = 90}, [](const FrameContext& ctx) {
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
            l.text("back_label", {.text = "BACK", .pos = {20.0f, 30.0f, 0.0f}, .font_size = 14.0f, .color = Color{1.0f, 1.0f, 1.0f, 0.40f}});
        });

        s.layer("card_mid", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({0.0f, 0.0f, 0.0f});
            l.rounded_rect("mid_rect", {
                .size = {350.0f, 220.0f},
                .radius = 16.0f,
                .color = Color{0.25f, 0.52f, 1.0f, 0.92f},
                .stroke = { .enabled = true, .color = Color{0.0f, 0.9f, 1.0f, 0.35f}, .width = 1.25f }
            });
            l.text("mid_label", {.text = "MID", .pos = {20.0f, 30.0f, 0.0f}, .font_size = 14.0f, .color = Color{1.0f, 1.0f, 1.0f, 0.60f}});
        });

        s.layer("card_front", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({0.0f, 0.0f, -200.0f});
            l.rounded_rect("front_rect", {
                .size = {300.0f, 190.0f},
                .radius = 16.0f,
                .color = Color{0.99f, 0.44f, 0.82f, 1.0f},
                .stroke = { .enabled = true, .color = Color{0.0f, 0.9f, 1.0f, 0.5f}, .width = 1.5f }
            });
            l.text("front_label", {.text = "FRONT", .pos = {20.0f, 30.0f, 0.0f}, .font_size = 14.0f, .color = Color{1.0f, 1.0f, 1.0f, 0.80f}});
        });

        s.layer("bg_grid", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({0.0f, 0.0f, 500.0f});
            l.grid_background("grid", GridBackgroundParams{
                .size = {1920.0f, 1080.0f},
                .bg_color = {0.02f, 0.02f, 0.05f, 1.0f},
                .grid_color = {0.28f, 0.48f, 0.98f, 0.04f},
                .spacing = 100.0f,
                .centered = true
            });
        });

        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.orbit_yaw.key(0, -25.0f).key(90, 25.0f, EasingCurve{Easing::InOutCubic});
        shot.rig.orbit_pitch.key(0, -5.0f).key(90, 5.0f);
        shot.rig.orbit_radius.set(1000.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);

        shot.validator
            .register_layer_size("card_front", {300.0f, 190.0f})
            .register_layer_size("card_mid", {350.0f, 220.0f})
            .register_layer_size("card_back", {400.0f, 250.0f})
            .require_target_centered("camera_target", 3.0f)
            .require_visible("card_front", 0.95f)
            .require_visible("card_mid", 0.75f)
            .require_visible("card_back", 0.50f)
            .require_depth_order({"card_front", "card_mid", "card_back"});

        return camera_test_orchestrator(ctx, s, shot, "CameraOrbitTargetLockTest", {"card_front", "card_mid", "card_back"}, {0, 45, 90});
    });
}

// 2. CameraDollyPerspectiveScaleTest (Dolly close & far to check perspective areas ratio)
Composition camera_dolly_perspective_scale_test() {
    return composition({.name = "CameraDollyPerspectiveScaleTest", .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.null_layer("camera_target", [](NullBuilder& n) {
            n.position({0.0f, 0.0f, 0.0f});
        });

        s.layer("card_back", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({0.0f, 0.0f, 300.0f});
            l.rounded_rect("back_rect", {.size = {400.0f, 250.0f}, .radius = 16.0f, .color = Color{0.15f, 0.18f, 0.35f, 0.85f}});
        });

        s.layer("card_mid", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({0.0f, 0.0f, 0.0f});
            l.rounded_rect("mid_rect", {.size = {350.0f, 220.0f}, .radius = 16.0f, .color = Color{0.25f, 0.52f, 1.0f, 0.92f}});
        });

        s.layer("card_front", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({0.0f, 0.0f, -300.0f});
            l.rounded_rect("front_rect", {.size = {300.0f, 190.0f}, .radius = 16.0f, .color = Color{0.99f, 0.44f, 0.82f, 1.0f}});
        });

        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.orbit_radius.key(0, 1600.0f)
                              .key(45, 800.0f, EasingCurve{Easing::InOutCubic})
                              .key(90, 1600.0f, EasingCurve{Easing::InOutCubic});
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);

        shot.validator
            .register_layer_size("card_front", {300.0f, 190.0f})
            .register_layer_size("card_mid", {350.0f, 220.0f})
            .register_layer_size("card_back", {400.0f, 250.0f})
            .require_depth_order({"card_front", "card_mid", "card_back"});

        return camera_test_orchestrator(ctx, s, shot, "CameraDollyPerspectiveScaleTest", {}, {0, 45, 90});
    });
}

// 3. CameraParentNullRigTest (Complex parenting null transformations resolving)
Composition camera_parent_null_rig_test() {
    return composition({.name = "CameraParentNullRigTest", .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        // Rotating null parent controller
        s.null_layer("camera_rig_null", [ctx](NullBuilder& n) {
            n.position({0.0f, 0.0f, 0.0f});
            n.rotation({0.0f, ctx.progress() * 45.0f, 0.0f});
        });

        s.null_layer("card_null", [](NullBuilder& n) {
            n.position({0.0f, 0.0f, -100.0f});
            n.parent("camera_rig_null");
        });

        s.null_layer("camera_target", [](NullBuilder& n) {
            n.position({0.0f, 0.0f, 0.0f});
            n.parent("camera_rig_null");
        });

        // 2 Children layers parented to card_null
        s.layer("card_a", [](LayerBuilder& l) {
            l.enable_3d().position({150.0f, 0.0f, 0.0f}).parent("card_null");
            l.rounded_rect("rect_a", {.size = {200.0f, 150.0f}, .radius = 8.0f, .color = Color{0.25f, 0.52f, 1.0f, 0.9f}});
            l.text("lbl_a", {.text = "A", .pos = {20.0f, 30.0f, 0.0f}});
        });

        s.layer("card_b", [](LayerBuilder& l) {
            l.enable_3d().position({-150.0f, 0.0f, 0.0f}).parent("card_null");
            l.rounded_rect("rect_b", {.size = {200.0f, 150.0f}, .radius = 8.0f, .color = Color{0.99f, 0.44f, 0.82f, 0.9f}});
            l.text("lbl_b", {.text = "B", .pos = {20.0f, 30.0f, 0.0f}});
        });

        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.parent_name = "camera_rig_null";
        shot.rig.orbit_radius.set(1000.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);

        shot.validator
            .register_layer_size("card_a", {200.0f, 150.0f})
            .register_layer_size("card_b", {200.0f, 150.0f})
            .require_target_centered("camera_target", 3.0f);

        return camera_test_orchestrator(ctx, s, shot, "CameraParentNullRigTest", {}, {0, 45, 90});
    });
}

// 4. CameraRollPanTiltGridTest (Roll, Pitch tilt, and Yaw pan separated rotations check)
Composition camera_roll_pan_tilt_grid_test() {
    return composition({.name = "CameraRollPanTiltGridTest", .duration = 90}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);

        s.null_layer("camera_target", [](NullBuilder& n) {
            n.position({0.0f, 0.0f, 0.0f});
        });

        s.layer("bg_grid", [](LayerBuilder& l) {
            l.cache_static().enable_3d().position({0.0f, 0.0f, 500.0f});
            l.grid_background("grid", GridBackgroundParams{
                .size = {1920.0f, 1080.0f},
                .bg_color = {0.02f, 0.02f, 0.05f, 1.0f},
                .grid_color = {0.28f, 0.48f, 0.98f, 0.06f},
                .spacing = 100.0f,
                .centered = true
            });
        });

        CameraShotProfile shot;
        shot.rig.mode = CameraRigMode::TwoNode;
        shot.rig.target_name = "camera_target";
        shot.rig.roll.key(0, 0.0f).key(30, 10.0f).key(90, 10.0f);
        shot.rig.orbit_pitch.key(0, 0.0f).key(30, 0.0f).key(60, 15.0f).key(90, 15.0f);
        shot.rig.orbit_yaw.key(0, 0.0f).key(60, 0.0f).key(90, 20.0f);
        shot.rig.orbit_radius.set(1000.0f);
        shot.rig.projection_mode = Camera2_5DProjectionMode::Fov;
        shot.rig.fov_deg.set(50.0f);

        shot.validator.require_target_centered("camera_target", 3.0f);

        return camera_test_orchestrator(ctx, s, shot, "CameraRollPanTiltGridTest", {}, {0, 30, 60, 90});
    });
}

// 5. Safe Framing Aspect Ratio implementations
Scene camera_safe_framing_aspect_ratio_test_impl(const FrameContext& ctx, i32 width, i32 height, const std::string& comp_name) {
    SceneBuilder s(ctx);

    s.null_layer("camera_target", [](NullBuilder& n) {
        n.position({0.0f, 0.0f, 0.0f});
    });

    s.layer("card_mid", [](LayerBuilder& l) {
        l.cache_static().enable_3d().position({0.0f, 0.0f, 0.0f});
        l.rounded_rect("mid_rect", {.size = {350.0f, 220.0f}, .radius = 16.0f, .color = Color{0.25f, 0.52f, 1.0f, 0.92f}});
    });

    CameraShotProfile shot;
    shot.rig = camera_rig_presets::orbit_reveal("camera_target");
    shot.validator
        .register_layer_size("card_mid", {350.0f, 220.0f})
        .require_inside_safe_area("card_mid", 0.08f);
    shot.auto_fit = true;

    return camera_test_orchestrator(ctx, s, shot, comp_name, {"card_mid"}, {0, 45, 90});
}

Composition camera_safe_framing_aspect_ratio_16_9() {
    return composition({.name = "CameraSafeFramingAspectRatioTest_16_9", .width = 1920, .height = 1080, .duration = 90}, [](const FrameContext& ctx) {
        return camera_safe_framing_aspect_ratio_test_impl(ctx, 1920, 1080, "CameraSafeFramingAspectRatioTest_16_9");
    });
}

Composition camera_safe_framing_aspect_ratio_1_1() {
    return composition({.name = "CameraSafeFramingAspectRatioTest_1_1", .width = 1080, .height = 1080, .duration = 90}, [](const FrameContext& ctx) {
        return camera_safe_framing_aspect_ratio_test_impl(ctx, 1080, 1080, "CameraSafeFramingAspectRatioTest_1_1");
    });
}

Composition camera_safe_framing_aspect_ratio_9_16() {
    return composition({.name = "CameraSafeFramingAspectRatioTest_9_16", .width = 1080, .height = 1920, .duration = 90}, [](const FrameContext& ctx) {
        return camera_safe_framing_aspect_ratio_test_impl(ctx, 1080, 1920, "CameraSafeFramingAspectRatioTest_9_16");
    });
}

Composition camera_safe_framing_aspect_ratio_4_5() {
    return composition({.name = "CameraSafeFramingAspectRatioTest_4_5", .width = 1080, .height = 1350, .duration = 90}, [](const FrameContext& ctx) {
        return camera_safe_framing_aspect_ratio_test_impl(ctx, 1080, 1350, "CameraSafeFramingAspectRatioTest_4_5");
    });
}

void register_all() {}

} // namespace chronon3d::content::two_point_five_d

CHRONON_REGISTER_COMPOSITION("ParallaxSimple", chronon3d::content::two_point_five_d::parallax_simple)
CHRONON_REGISTER_COMPOSITION("DepthScene",     chronon3d::content::two_point_five_d::depth_scene)
CHRONON_REGISTER_COMPOSITION("CardFlip",       chronon3d::content::two_point_five_d::card_flip)
CHRONON_REGISTER_COMPOSITION("ParallaxText",   chronon3d::content::two_point_five_d::parallax_text)
CHRONON_REGISTER_COMPOSITION("CameraOrbitTargetLockTest", chronon3d::content::two_point_five_d::camera_orbit_target_lock_test)
CHRONON_REGISTER_COMPOSITION("CameraDollyPerspectiveScaleTest", chronon3d::content::two_point_five_d::camera_dolly_perspective_scale_test)
CHRONON_REGISTER_COMPOSITION("CameraParentNullRigTest", chronon3d::content::two_point_five_d::camera_parent_null_rig_test)
CHRONON_REGISTER_COMPOSITION("CameraRollPanTiltGridTest", chronon3d::content::two_point_five_d::camera_roll_pan_tilt_grid_test)
CHRONON_REGISTER_COMPOSITION("CameraSafeFramingAspectRatioTest_16_9", chronon3d::content::two_point_five_d::camera_safe_framing_aspect_ratio_16_9)
CHRONON_REGISTER_COMPOSITION("CameraSafeFramingAspectRatioTest_1_1", chronon3d::content::two_point_five_d::camera_safe_framing_aspect_ratio_1_1)
CHRONON_REGISTER_COMPOSITION("CameraSafeFramingAspectRatioTest_9_16", chronon3d::content::two_point_five_d::camera_safe_framing_aspect_ratio_9_16)
CHRONON_REGISTER_COMPOSITION("CameraSafeFramingAspectRatioTest_4_5", chronon3d::content::two_point_five_d::camera_safe_framing_aspect_ratio_4_5)

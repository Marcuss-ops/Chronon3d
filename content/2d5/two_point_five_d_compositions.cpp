#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <chronon3d/math/color.hpp>

#include <cmath>

namespace chronon3d::content::two_point_five_d {
namespace {

constexpr f32 W = 1920.0f;
constexpr f32 H = 1080.0f;

static void make_text(LayerBuilder& l, const std::string& name, const std::string& text,
                       f32 font_size, Color color, TextAlign align, f32 tracking,
                       Vec2 sz = {W * 0.7f, 160.0f}, Vec3 pos = {0.0f, 0.0f, 0.0f}) {
    l.text(name, TextParams{
        .text = text,
        .size = sz,
        .pos = pos,
        .font_family = "Inter",
        .font_weight = 800,
        .font_size = font_size,
        .color = color,
        .align = align,
        .line_height = 1.2f,
        .tracking = tracking,
    });
}

// ── ParallaxSimple ───────────────────────────────────────────────────────
Composition parallax_simple() {
    return composition({
        .name = "ParallaxSimple",
        .width = 1920, .height = 1080,
        .duration = 120,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 cam_x = -p * 400.0f;

        s.camera().set(chronon3d::camera_motion::dolly(p, {
            .from_z = -900.0f,
            .to_z = -720.0f,
            .target = {0.0f, 0.0f, 0.0f},
            .zoom = 800.0f,
        }));

        s.layer("far_bg", [&](LayerBuilder& l) {
            l.enable_3d().position({cam_x * 0.2f, 0, 300});
            l.rect("far", {
                .size = {W * 2.0f, H},
                .color = {0.02f, 0.03f, 0.08f, 1.0f},
                .pos = {0, 0, 0},
            });
        });

        s.layer("mid_bg", [&](LayerBuilder& l) {
            l.enable_3d().position({cam_x * 0.5f, 0, 100});
            l.rect("mid", {
                .size = {W * 1.5f, H},
                .color = {0.04f, 0.06f, 0.12f, 1.0f},
                .pos = {0, 0, 0},
            });
        });

        for (int i = 0; i < 5; ++i) {
            s.layer("mid_elem_" + std::to_string(i), [&, i](LayerBuilder& l) {
                f32 ex = static_cast<f32>(i) * 500.0f - 1000.0f;
                l.enable_3d().position({ex + cam_x * 0.5f, static_cast<f32>(i % 3 - 1) * 200.0f, 100});
                l.circle("c", {
                    .radius = 20.0f + static_cast<f32>(i) * 10.0f,
                    .color = {0.25f, 0.52f, 1.0f, 0.15f},
                });
            });
        }

        s.layer("fg", [&](LayerBuilder& l) {
            l.enable_3d().position({cam_x, 0, -200});
            l.rect("fg_rect", {
                .size = {W, 300.0f},
                .color = {0.06f, 0.08f, 0.14f, 1.0f},
                .pos = {0, 400, 0},
            });
        });

        for (int i = 0; i < 3; ++i) {
            s.layer("fg_box_" + std::to_string(i), [&, i](LayerBuilder& l) {
                f32 ex = static_cast<f32>(i) * 600.0f - 600.0f;
                l.enable_3d().position({ex + cam_x, -50 - static_cast<f32>(i) * 80.0f, -200});
                l.rounded_rect("box", {
                    .size = {180.0f, 140.0f},
                    .radius = 12.0f,
                    .color = {0.25f, 0.52f, 1.0f, 0.4f},
                    .pos = {0, 0, 0},
                });
            });
        }

        s.layer("label", [&](LayerBuilder& l) {
            l.opacity(0.7f).pin_to(Anchor::BottomLeft, 40.0f);
            make_text(l, "label_text", "Parallax Demo  |  Far(0.2x)  Mid(0.5x)  FG(1.0x)",
                      22.0f, {0.6f, 0.7f, 0.9f, 1}, TextAlign::Left, 1.5f,
                      {W * 0.5f, 30.0f});
        });

        return s.build();
    });
}

// ── DepthScene ───────────────────────────────────────────────────────────
Composition depth_scene() {
    return composition({
        .name = "DepthScene",
        .width = 1920, .height = 1080,
        .duration = 90,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 cam_z = chronon3d::camera_motion::lerp(-800.0f, -500.0f, chronon3d::camera_motion::smoothstep(p));

        s.camera().set(chronon3d::camera_motion::push_in_tilt(p, {
            .from_z = -800.0f,
            .to_z = -500.0f,
            .from_tilt = -4.0f,
            .to_tilt = 4.0f,
            .zoom = 800.0f,
        }));
        s.ambient_light(Color{1, 1, 1, 1}, 0.2f);
        s.directional_light(Vec3{-0.5f, 1.0f, -0.5f}, Color{1, 1, 1, 1}, 0.8f);

        s.layer("far_layer", [](LayerBuilder& l) {
            l.enable_3d().position({0, 0, 200});
            l.rect("far_rect", {
                .size = {1600.0f, 900.0f},
                .color = {0.05f, 0.08f, 0.20f, 1.0f},
            });
        });

        s.layer("mid_layer", [&](LayerBuilder& l) {
            l.enable_3d().position({0, 0, 0});
            l.rounded_rect("mid_rect", {
                .size = {800.0f, 500.0f},
                .radius = 24.0f,
                .color = {0.12f, 0.15f, 0.25f, 1.0f},
            });
            l.rect("inner", {
                .size = {400.0f, 200.0f},
                .color = {0.25f, 0.52f, 1.0f, 0.12f},
                .pos = {0, 0, 0},
            });
        });

        s.layer("near_layer", [&](LayerBuilder& l) {
            l.enable_3d().position({0, -100, -150});
            l.rounded_rect("near_rect", {
                .size = {300.0f, 80.0f},
                .radius = 16.0f,
                .color = {0.25f, 0.52f, 1.0f, 0.6f},
            });
        });

        for (int i = 0; i < 4; ++i) {
            f32 angle = static_cast<f32>(i) * 1.5708f;
            s.layer("pillar_" + std::to_string(i), [&, i, angle](LayerBuilder& l) {
                f32 px = std::cos(angle) * 500.0f;
                f32 pz = std::sin(angle) * 100.0f;
                l.enable_3d().position({px, 0, pz})
                 .casts_shadows(true).accepts_shadows(true);
                l.fake_box3d("box", {
                    .pos = {0, 0, 0},
                    .size = {40.0f, 300.0f},
                    .depth = 40.0f,
                    .color = {0.2f, 0.3f, 0.5f, 1.0f},
                    .contact_shadow = true,
                    .floor_y = -250.0f,
                });
            });
        }

        s.layer("floor", [](LayerBuilder& l) {
            l.enable_3d().position({0, -250, 0}).accepts_shadows(true);
            l.rect("floor_rect", {
                .size = {2000.0f, 2000.0f},
                .color = {0.03f, 0.04f, 0.06f, 1.0f},
            });
        });

        s.layer("info", [&](LayerBuilder& l) {
            l.pin_to(Anchor::BottomRight, 40.0f);
            make_text(l, "info_text", "Depth: " + std::to_string(static_cast<i32>(cam_z)),
                      20.0f, {0.6f, 0.7f, 0.9f, 0.5f}, TextAlign::Right, 1.0f,
                      {200.0f, 30.0f});
        });

        return s.build();
    });
}

// ── CardFlip ─────────────────────────────────────────────────────────────
Composition card_flip() {
    return composition({
        .name = "CardFlip",
        .width = 1920, .height = 1080,
        .duration = 90,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 angle_y = p * 360.0f;
        f32 scale_x = std::cos(angle_y * 0.0174533f);
        bool show_front = scale_x > 0.0f;

        s.camera().set(chronon3d::camera_motion::orbit_small(p, 800.0f));

        s.layer("bg", [](LayerBuilder& l) {
            l.fill(Color{0.008f, 0.012f, 0.025f, 1.0f});
        });

        s.layer("card", [&](LayerBuilder& l) {
            l.enable_3d().position({0, 0, 0})
             .rotate({0, angle_y, 0})
             .casts_shadows(true).accepts_shadows(true);

            if (show_front) {
                l.rounded_rect("front", {
                    .size = {400.0f, 560.0f},
                    .radius = 20.0f,
                    .color = {0.12f, 0.15f, 0.25f, 1.0f},
                });
                l.rect("header", {
                    .size = {360.0f, 80.0f},
                    .color = {0.25f, 0.52f, 1.0f, 0.3f},
                    .pos = {0, -200, 0.1f},
                });
                make_text(l, "front_label", "FLIP", 56.0f, {1, 1, 1, 1},
                          TextAlign::Center, 8.0f, {320.0f, 80.0f}, {0, 0, 0.2f});
            } else {
                l.rounded_rect("back", {
                    .size = {400.0f, 560.0f},
                    .radius = 20.0f,
                    .color = {0.08f, 0.10f, 0.18f, 1.0f},
                });
                make_text(l, "back_label", "2.5D", 56.0f, {0.4f, 0.6f, 0.9f, 1},
                          TextAlign::Center, 8.0f, {320.0f, 80.0f}, {0, 0, 0.2f});
            }
        });

        s.layer("floor_shadow", [&](LayerBuilder& l) {
            l.enable_3d().position({0, -350, 0});
            f32 shadow_scale = std::abs(scale_x) * 0.8f + 0.2f;
            l.rect("shadow_rect", {
                .size = {400.0f * shadow_scale, 60.0f},
                .color = {0, 0, 0, 0.2f * (1.0f - std::abs(scale_x) * 0.3f)},
            });
        });

        return s.build();
    });
}

// ── ParallaxText ─────────────────────────────────────────────────────────
Composition parallax_text() {
    return composition({
        .name = "ParallaxText",
        .width = 1920, .height = 1080,
        .duration = 120,
    }, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        f32 p = ctx.progress();
        f32 move = -p * 300.0f;

        s.camera().set(chronon3d::camera_motion::parallax_sweep(p, 120.0f, -900.0f, 800.0f));

        for (int i = 0; i < 20; ++i) {
            s.layer("star_" + std::to_string(i), [&, i](LayerBuilder& l) {
                f32 sx = (static_cast<f32>(i) * 200.0f - 2000.0f) + move * 0.1f;
                f32 sy = std::sin(static_cast<f32>(i) * 1.7f) * 400.0f;
                l.enable_3d().position({sx, sy, 400})
                 .opacity(0.3f + 0.2f * std::sin(p * 3.0f + static_cast<f32>(i)));
                l.circle("dot", {
                    .radius = 3.0f,
                    .color = {1, 1, 1, 1},
                });
            });
        }

        for (int i = 0; i < 6; ++i) {
            s.layer("mid_" + std::to_string(i), [&, i](LayerBuilder& l) {
                f32 mx = (static_cast<f32>(i) * 400.0f - 1000.0f) + move * 0.4f;
                l.enable_3d().position({mx, 0, 100});
                l.rounded_rect("shape", {
                    .size = {120.0f, 80.0f},
                    .radius = 10.0f,
                    .color = {0.25f, 0.52f, 1.0f, 0.12f},
                });
            });
        }

        s.layer("parallax_title", [&](LayerBuilder& l) {
            f32 op = std::min(1.0f, p * 3.0f);
            l.opacity(op).pin_to(Anchor::Center);
            make_text(l, "title", "2.5D PARALLAX", 80.0f, {1, 1, 1, 1},
                      TextAlign::Center, 10.0f, {W * 0.85f, 120.0f});
        });

        s.layer("parallax_sub", [&](LayerBuilder& l) {
            f32 op = std::min(1.0f, (p - 0.1f) * 4.0f);
            l.opacity(op).pin_to(Anchor::Center).position({0, 90, 0});
            make_text(l, "sub", "Multi-layer depth with motion", 30.0f, {0.7f, 0.7f, 0.85f, 1},
                      TextAlign::Center, 2.0f, {W * 0.6f, 50.0f});
        });

        return s.build();
    });
}

} // anonymous namespace

void register_all() {}

} // namespace chronon3d::content::two_point_five_d

CHRONON_REGISTER_COMPOSITION("ParallaxSimple", chronon3d::content::two_point_five_d::parallax_simple)
CHRONON_REGISTER_COMPOSITION("DepthScene",     chronon3d::content::two_point_five_d::depth_scene)
CHRONON_REGISTER_COMPOSITION("CardFlip",       chronon3d::content::two_point_five_d::card_flip)
CHRONON_REGISTER_COMPOSITION("ParallaxText",   chronon3d::content::two_point_five_d::parallax_text)

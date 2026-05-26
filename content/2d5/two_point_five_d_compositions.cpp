#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/presets/parallax_layer.hpp>
#include <chronon3d/presets/text_spec.hpp>

#include <cmath>
#include <string>
#include <vector>

namespace chronon3d::content::two_point_five_d {

using presets::TextSpec;
using presets::ParallaxLayer;

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

        ParallaxLayer far_l; far_l.set_depth(300, 0.2f);
        ParallaxLayer mid_l; mid_l.set_depth(100, 0.5f);
        ParallaxLayer fg_l;  fg_l.set_depth(-200, 1.0f);

        s.layer("far_bg", [&](auto& l) { 
            far_l.apply(l, cam_x);
            l.rect("far", {.size = {W * 2, H}, .color = {0.02f, 0.03f, 0.08f, 1}});
        });

        s.layer("mid_bg", [&](auto& l) {
            mid_l.apply(l, cam_x);
            l.rect("mid", {.size = {W * 1.5f, H}, .color = {0.04f, 0.06f, 0.12f, 1}});
        });

        for (int i = 0; i < 5; ++i) {
            s.layer("mid_elem_" + std::to_string(i), [&](auto& l) {
                mid_l.apply(l, cam_x, {i * 500.0f - 1000.0f, static_cast<f32>(i % 3 - 1) * 200.0f, 0});
                l.circle("c", {.radius = 20.0f + i * 10.0f, .color = {0.25f, 0.52f, 1, 0.15f}});
            });
        }

        s.layer("fg", [&](auto& l) {
            fg_l.apply(l, cam_x);
            l.rect("fg_rect", {.size = {W, 300}, .color = {0.06f, 0.08f, 0.14f, 1}, .pos = {0, 400, 0}});
        });

        for (int i = 0; i < 3; ++i) {
            s.layer("fg_box_" + std::to_string(i), [&](auto& l) {
                fg_l.apply(l, cam_x, {i * 600.0f - 600.0f, -50.0f - i * 80.0f, 0});
                l.rounded_rect("box", {.size = {180, 140}, .radius = 12, .color = {0.25f, 0.52f, 1, 0.4f}});
            });
        }

        s.layer("label", [&](auto& l) {
            l.opacity(0.7f).pin_to(Anchor::BottomLeft, 40.0f);
            TextSpec("Parallax Demo  |  Far(0.2x)  Mid(0.5x)  FG(1.0x)").set_font(22, 1.5f).set_color({0.6f, 0.7f, 0.9f, 1}).set_align(TextAlign::Left).draw(l, "txt", {W*0.5f, 30});
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
            TextSpec("Depth: " + std::to_string(static_cast<i32>(cam_z))).set_font(20).set_color({0.6f, 0.7f, 0.9f, 0.5f}).set_align(TextAlign::Right).draw(l, "txt", {200, 30});
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
                TextSpec("FLIP").set_font(56, 8).draw(l, "label", {320, 80}, {0, 0, 0.2f});
            } else {
                l.rounded_rect("back", {.size = {400, 560}, .radius = 20, .color = {0.08f, 0.10f, 0.18f, 1}});
                TextSpec("2.5D").set_font(56, 8).set_color({0.4f, 0.6f, 0.9f, 1}).draw(l, "label", {320, 80}, {0, 0, 0.2f});
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
            TextSpec("2.5D PARALLAX").set_font(80, 10).draw(l, "title", {W * 0.85f, 120});
        });

        s.layer("parallax_sub", [&](auto& l) {
            l.opacity(std::min(1.0f, (p - 0.1f) * 4.0f)).pin_to(Anchor::Center).position({0, 90, 0});
            TextSpec("Multi-layer depth with motion").set_font(30, 2).set_color({0.7f, 0.7f, 0.85f, 1}).draw(l, "sub", {W * 0.6f, 50});
        });

        return s.build();
    });
}

void register_all() {}

} // namespace chronon3d::content::two_point_five_d

CHRONON_REGISTER_COMPOSITION("ParallaxSimple", chronon3d::content::two_point_five_d::parallax_simple)
CHRONON_REGISTER_COMPOSITION("DepthScene",     chronon3d::content::two_point_five_d::depth_scene)
CHRONON_REGISTER_COMPOSITION("CardFlip",       chronon3d::content::two_point_five_d::card_flip)
CHRONON_REGISTER_COMPOSITION("ParallaxText",   chronon3d::content::two_point_five_d::parallax_text)

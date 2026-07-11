#include "reference_2_5d_suite_helpers.hpp"
#include "reference_2_5d_suite.hpp"
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <cmath>

namespace chronon3d::content::effects {

using namespace ref_2_5d_helpers;

// ═════════════════════════════════════════════════════════════════════════════
// Atmospheric scenes — DepthFog, ExtremePerspective, YRotationText
// ═════════════════════════════════════════════════════════════════════════════

Composition depth_fog_test() {
    return composition({.name = "DepthFogTest", .width = kW, .height = kH, .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        s.camera().set(camera_motion::push_in_tilt(p, {.from_z = -940.0f, .to_z = -640.0f, .from_tilt = -10.0f, .to_tilt = 3.0f, .zoom = 900.0f}));
        s.camera().fov(40.0f);
        apply_cinematic_lighting(s, {0.10f, 0.14f, 0.24f, 1.0f}, 0.24f, {-0.35f, 0.95f, -0.55f}, {0.92f, 0.98f, 1.0f, 1.0f}, 1.00f);
        deep_bg(s, Color{0.015f, 0.025f, 0.055f, 1.0f}, Color{0.005f, 0.010f, 0.030f, 1.0f});
        add_header(s, "07", "DEPTH FOG TEST", "Verifica della foschia per profondità.");
        s.layer("fog", [](LayerBuilder& l) { l.position({0.0f, 160.0f, 0.0f}); l.opacity(0.82f); l.glow(GlowParams{.radius = 90.0f, .intensity = 0.9f, .color = {0.26f, 0.62f, 1.0f, 1.0f}}); l.circle("mist", {.radius = 280.0f, .color = {0.10f, 0.40f, 1.0f, 0.16f}, .pos = {0.0f, 0.0f, 0.0f}}); });
        s.layer("mountains_back", [](LayerBuilder& l) { l.position({0.0f, 120.0f, 180.0f}); l.opacity(0.86f); l.rounded_rect("left", {.size = {420.0f, 260.0f}, .radius = 22.0f, .color = {0.04f, 0.08f, 0.16f, 1.0f}, .pos = {-420.0f, 60.0f, 0.0f}}); l.rounded_rect("center", {.size = {620.0f, 360.0f}, .radius = 26.0f, .color = {0.05f, 0.10f, 0.20f, 1.0f}, .pos = {0.0f, -20.0f, 0.0f}}); l.rounded_rect("right", {.size = {360.0f, 220.0f}, .radius = 20.0f, .color = {0.04f, 0.08f, 0.16f, 1.0f}, .pos = {430.0f, 70.0f, 0.0f}}); });
        s.layer("mountains_mid", [](LayerBuilder& l) { l.position({0.0f, 180.0f, 20.0f}); l.opacity(0.92f); l.rounded_rect("left", {.size = {500.0f, 300.0f}, .radius = 28.0f, .color = {0.05f, 0.11f, 0.24f, 1.0f}, .pos = {-520.0f, 55.0f, 0.0f}}); l.rounded_rect("center", {.size = {760.0f, 430.0f}, .radius = 34.0f, .color = {0.08f, 0.14f, 0.30f, 1.0f}, .pos = {0.0f, -10.0f, 0.0f}}); l.rounded_rect("right", {.size = {460.0f, 260.0f}, .radius = 24.0f, .color = {0.05f, 0.10f, 0.22f, 1.0f}, .pos = {520.0f, 70.0f, 0.0f}}); l.glow(GlowParams{.radius = 44.0f, .intensity = 0.75f, .color = {0.14f, 0.42f, 1.0f, 1.0f}}); });
        s.layer("ground", [](LayerBuilder& l) { l.position({0.0f, 360.0f, 0.0f}); l.rect("floor", {.size = {1500.0f, 240.0f}, .color = {0.03f, 0.05f, 0.12f, 1.0f}, .pos = {0.0f, 0.0f, 0.0f}, .fill = FillStyle::linear({0.0f, 0.0f}, {0.0f, 1.0f}, {{0.0f, {0.03f, 0.05f, 0.14f, 0.0f}}, {1.0f, {0.03f, 0.05f, 0.12f, 0.90f}}})}); l.glow(GlowParams{.radius = 36.0f, .intensity = 0.8f, .color = {0.10f, 0.46f, 1.0f, 1.0f}}); });
        s.layer("title", [](LayerBuilder& l) { l.position({0.0f, -10.0f, 0.0f}); l.text("future", TextSpec{.content = {.value = "FUTURE"},.position = {0.0f, -78.0f, 0.0f},.font = {.font_path = "assets/fonts/Poppins-Bold.ttf", .font_family = "Inter", .font_weight = 900, .font_size = 44.0f},
                                                                                                  .layout = {.box = {700.0f, 80.0f}, .align = TextAlign::Center, .vertical_align = VerticalAlign::Middle},.appearance = {.color = {0.26f, 0.48f, 0.80f, 0.36f}},}); l.glow(GlowParams{.radius = 40.0f, .intensity = 1.2f, .color = {0.22f, 0.72f, 1.0f, 1.0f}}); l.text("innovate", TextSpec{.content = {.value = "INNOVATE"},.position = {0.0f, -8.0f, 0.0f},.font = {.font_path = "assets/fonts/Poppins-Bold.ttf", .font_family = "Inter", .font_weight = 900, .font_size = 56.0f},
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       .layout = {.box = {780.0f, 96.0f}, .align = TextAlign::Center, .vertical_align = VerticalAlign::Middle},.appearance = {.color = {0.40f, 0.64f, 0.92f, 0.52f}},}); l.text("build", TextSpec{.content = {.value = "BUILD"},.position = {0.0f, 104.0f, 0.0f},.font = {.font_path = "assets/fonts/Poppins-Bold.ttf", .font_family = "Inter", .font_weight = 900, .font_size = 94.0f},
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             .layout = {.box = {920.0f, 150.0f}, .align = TextAlign::Center, .vertical_align = VerticalAlign::Middle, .tracking = 0.0f},.appearance = {.color = {0.82f, 0.93f, 1.0f, 1.0f}},}); });
        return s.build();
    });
}

Composition extreme_perspective_test() {
    return composition({.name = "ExtremePerspectiveTest", .width = kW, .height = kH, .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        s.camera().set(camera_motion::orbit_small(p, 1020.0f));
        s.camera().fov(34.0f);
        apply_cinematic_lighting(s, {0.10f, 0.16f, 0.28f, 1.0f}, 0.20f, {-0.35f, 1.0f, -0.50f}, {0.86f, 0.96f, 1.0f, 1.0f}, 1.15f);
        deep_bg(s, Color{0.012f, 0.030f, 0.090f, 1.0f}, Color{0.020f, 0.040f, 0.140f, 1.0f});
        add_header(s, "03", "EXTREME PERSPECTIVE TEST", "Prospettiva estrema per stressare il sistema.");
        add_neon_floor(s, {0.22f, 0.68f, 1.0f, 0.15f}, 72.0f);
        s.layer("spot", [](LayerBuilder& l) { l.position({0.0f, -250.0f, 0.0f}); l.glow(GlowParams{.radius = 220.0f, .intensity = 1.0f, .color = {0.18f, 0.72f, 1.0f, 1.0f}}); l.circle("c", {.radius = 180.0f, .color = {0.22f, 0.62f, 1.0f, 0.28f}, .pos = {0.0f, 0.0f, 0.0f}}); });
        s.layer("title", [=](LayerBuilder& l) { l.enable_3d().position({-100.0f, 40.0f, -40.0f}).rotate({0.0f, -22.0f, 0.0f}); apply_depth_material(l, false, true, 0.92f, 0.22f, 56.0f, 0.78f); l.glow(GlowParams{.radius = 18.0f, .intensity = 0.45f, .color = {0.42f, 0.76f, 1.0f, 1.0f}}); l.drop_shadow({0.0f, 20.0f}, {0.02f, 0.10f, 0.25f, 0.68f}, 24.0f); l.text("txt", TextSpec{.content = {.value = "MASTERCLASS"},.position = {0.0f, 0.0f, 0.0f},.font = {.font_path = "assets/fonts/Poppins-Bold.ttf", .font_family = "Inter", .font_weight = 900, .font_size = 104.0f},
                                                                                                                                                                                                                                                                                                                                                                                .layout = {.box = {1080.0f, 160.0f}, .align = TextAlign::Center, .vertical_align = VerticalAlign::Middle, .tracking = -2.0f},.appearance = {.color = {0.94f, 0.97f, 1.0f, 1.0f}, .paint = {.fill = {0.94f, 0.97f, 1.0f, 1.0f}, .fill_style = Fill::linear({0.0f, 0.0f}, {0.0f, 1.0f}, {{0.0f, {1.0f, 1.0f, 1.0f, 1.0f}}, {0.55f, {0.82f, 0.90f, 1.0f, 1.0f}}, {1.0f, {0.50f, 0.70f, 1.0f, 1.0f}}}), .stroke_enabled = true, .stroke_color = {0.18f, 0.28f, 0.58f, 0.55f}, .stroke_width = 2.0f}},}); });
        return s.build();
    });
}

Composition y_rotation_text_test() {
    return composition({.name = "YRotationTextTest", .width = kW, .height = kH, .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        s.camera().set(camera_motion::orbit_small(p, 880.0f));
        s.camera().fov(44.0f);
        apply_cinematic_lighting(s, {0.12f, 0.10f, 0.20f, 1.0f}, 0.24f, {-0.40f, 1.0f, -0.30f}, {0.92f, 0.82f, 1.0f, 1.0f}, 1.00f);
        deep_bg(s, Color{0.012f, 0.010f, 0.045f, 1.0f}, Color{0.050f, 0.015f, 0.110f, 1.0f});
        add_header(s, "02", "Y-ROTATION TEXT TEST", "Test rotazione sull'asse Y.");
        add_neon_floor(s, {0.78f, 0.20f, 1.0f, 0.18f}, 84.0f);
        s.layer("panel", [](LayerBuilder& l) { l.position({0.0f, 20.0f, 0.0f}); apply_depth_material(l, true, true, 0.78f, 0.18f, 42.0f, 0.68f); l.rounded_rect("bg", {.size = {920.0f, 360.0f}, .radius = 28.0f, .color = {0.16f, 0.08f, 0.26f, 0.74f}, .pos = {0.0f, 0.0f, 0.0f}}); l.glow(GlowParams{.radius = 24.0f, .intensity = 0.9f, .color = {0.76f, 0.34f, 1.0f, 1.0f}}); });
        s.layer("hero", [=](LayerBuilder& l) { l.enable_3d().position({-20.0f, -6.0f, -30.0f}).rotate({3.5f, 10.0f + std::sin(p * 6.28318f) * 8.0f, 0.0f}); apply_depth_material(l, false, true, 0.90f, 0.20f, 54.0f, 0.76f); l.glow(GlowParams{.radius = 20.0f, .intensity = 0.55f, .color = {0.82f, 0.42f, 1.0f, 1.0f}}); l.drop_shadow({0.0f, 20.0f}, {0.08f, 0.02f, 0.24f, 0.74f}, 20.0f); l.text("buttery", TextSpec{.content = {.value = "BUTTERY"},.position = {0.0f, -24.0f, 0.0f},.font = {.font_path = "assets/fonts/Poppins-Bold.ttf", .font_family = "Inter", .font_weight = 900, .font_size = 92.0f},
                                                                                                                                                                                                                                                                                                                                                                                                                 .layout = {.box = {700.0f, 120.0f}, .align = TextAlign::Center, .vertical_align = VerticalAlign::Middle, .tracking = -3.0f},.appearance = {.color = {0.98f, 0.76f, 1.0f, 1.0f}, .paint = {.fill = {0.98f, 0.76f, 1.0f, 1.0f}, .fill_style = Fill::linear({0.0f, 0.0f}, {0.0f, 1.0f}, {{0.0f, {1.0f, 0.94f, 1.0f, 1.0f}}, {0.55f, {0.94f, 0.58f, 1.0f, 1.0f}}, {1.0f, {0.72f, 0.22f, 1.0f, 1.0f}}}), .stroke_enabled = true, .stroke_color = {0.24f, 0.02f, 0.30f, 0.56f}, .stroke_width = 1.8f}},}); l.rounded_rect("pill", {.size = {300.0f, 56.0f}, .radius = 18.0f, .color = {0.20f, 0.12f, 0.38f, 1.0f}, .pos = {0.0f, 112.0f, 0.0f}, .fill = FillStyle::linear({0.0f, 0.0f}, {1.0f, 0.0f}, {{0.0f, {0.30f, 0.14f, 0.72f, 1.0f}}, {1.0f, {0.56f, 0.22f, 0.92f, 1.0f}}})}); l.text("pill_txt", TextSpec{.content = {.value = "SMOOTH ANIMATION"},.position = {0.0f, 112.0f, 0.0f},.font = {.font_path = "assets/fonts/Poppins-Bold.ttf", .font_family = "Inter", .font_weight = 800, .font_size = 16.0f},
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       .layout = {.box = {300.0f, 56.0f}, .align = TextAlign::Center, .vertical_align = VerticalAlign::Middle, .tracking = 1.0f},.appearance = {.color = kWhite},}); });
        return s.build();
    });
}

} // namespace chronon3d::content::effects

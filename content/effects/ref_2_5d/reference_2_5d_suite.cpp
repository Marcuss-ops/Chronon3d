#include "../common/glow_test_common.hpp"

#include <chronon3d/scene/camera/camera_motion_presets.hpp>

#include <cmath>

namespace chronon3d::content::effects {

namespace {

constexpr Color kWhite{1.0f, 1.0f, 1.0f, 1.0f};

void add_header(SceneBuilder& s, const std::string& id, const std::string& title, const std::string& subtitle) {
    const f32 title_font_size = title.size() > 24 ? 20.0f : (title.size() > 20 ? 22.0f : 24.0f);
    const f32 subtitle_font_size = title.size() > 24 ? 13.0f : 14.0f;
    s.layer("header_" + id, [=](LayerBuilder& l) {
        l.position({-kHW + 42.0f, -kHH + 42.0f, 0.0f});
        l.rounded_rect("badge", {
            .size = {52.0f, 52.0f},
            .radius = 12.0f,
            .color = {0.34f, 0.38f, 0.78f, 1.0f},
            .pos = {0.0f, 0.0f, 0.0f},
            .fill = Fill::linear(
                {0.0f, 0.0f},
                {0.0f, 1.0f},
                {
                    {0.0f, {0.55f, 0.34f, 0.98f, 1.0f}},
                    {1.0f, {0.18f, 0.14f, 0.62f, 1.0f}},
                }
            )
        });
        l.text("badge_txt", {
            .text = id,
            .size = {52.0f, 52.0f},
            .pos = {0.0f, 0.0f, 0.0f},
            .font_path = "assets/fonts/Inter-Bold.ttf",
            .font_family = "Inter",
            .font_weight = 900,
            .font_style = "normal",
            .font_size = 24.0f,
            .color = kWhite,
            .align = TextAlign::Center,
            .vertical_align = VerticalAlign::Middle,
        });
        l.text("title", {
            .text = title,
            .size = {1060.0f, 36.0f},
            .pos = {84.0f, -2.0f, 0.0f},
            .font_path = "assets/fonts/Inter-Bold.ttf",
            .font_family = "Inter",
            .font_weight = 900,
            .font_style = "normal",
            .font_size = title_font_size,
            .color = kWhite,
            .align = TextAlign::Left,
            .vertical_align = VerticalAlign::Middle,
        });
        l.text("subtitle", {
            .text = subtitle,
            .size = {1060.0f, 24.0f},
            .pos = {84.0f, 28.0f, 0.0f},
            .font_path = "assets/fonts/Inter-Regular.ttf",
            .font_family = "Inter",
            .font_weight = 400,
            .font_style = "normal",
            .font_size = subtitle_font_size,
            .color = {0.78f, 0.82f, 0.92f, 1.0f},
            .align = TextAlign::Left,
            .vertical_align = VerticalAlign::Middle,
        });
    });
}

void add_neon_floor(SceneBuilder& s, Color grid, f32 spacing = 80.0f) {
    s.layer("floor_grid", [=](LayerBuilder& l) {
        l.position({0.0f, 250.0f, 0.0f});
        l.opacity(0.30f);
        l.grid_background("grid", {
            .size = {(f32)kW, (f32)kH * 0.7f},
            .bg_color = {0.0f, 0.0f, 0.0f, 0.0f},
            .grid_color = grid,
            .spacing = spacing,
            .minor_thickness = 1.0f,
            .major_thickness = 2.0f,
            .major_every = 4,
            .centered = true,
        });
    });
}

void apply_cinematic_lighting(SceneBuilder& s, Color ambient_color, f32 ambient_intensity, Vec3 dir, Color dir_color, f32 diffuse) {
    s.ambient_light(ambient_color, ambient_intensity);
    s.directional_light(dir, dir_color, diffuse);
}

Material2_5D make_depth_material(bool casts_shadows = true, bool accepts_shadows = true, f32 diffuse = 0.82f, f32 specular = 0.18f, f32 shininess = 48.0f, f32 ambient = 0.72f) {
    return Material2_5D{
        .accepts_lights = true,
        .casts_shadows = casts_shadows,
        .accepts_shadows = accepts_shadows,
        .diffuse = diffuse,
        .specular = specular,
        .shininess = shininess,
        .ambient_multiplier = ambient,
    };
}

void apply_depth_material(LayerBuilder& l, bool casts_shadows = true, bool accepts_shadows = true, f32 diffuse = 0.82f, f32 specular = 0.18f, f32 shininess = 48.0f, f32 ambient = 0.72f) {
    l.accepts_lights(true)
     .casts_shadows(casts_shadows)
     .accepts_shadows(accepts_shadows)
     .material(make_depth_material(casts_shadows, accepts_shadows, diffuse, specular, shininess, ambient));
}

void add_floating_orb(SceneBuilder& s, const std::string& id, Vec3 pos, f32 radius, Color color, f32 glow_radius) {
    s.layer("orb_" + id, [=](LayerBuilder& l) {
        l.position(pos);
        l.glow(glow_radius, 0.85f, color);
        l.circle("orb", {
            .radius = radius,
            .color = color,
            .pos = {0.0f, 0.0f, 0.0f},
        });
    });
}

void add_card(SceneBuilder& s, const std::string& id, Vec3 pos, Vec3 rot, Vec2 size, f32 radius, Color fill, Color glow, f32 blur = 18.0f) {
    s.layer("card_" + id, [=](LayerBuilder& l) {
        l.enable_3d().position(pos).rotate(rot);
        apply_depth_material(l, true, true, 0.80f, 0.20f, 56.0f, 0.68f);
        l.drop_shadow({0.0f, 22.0f}, {fill.r * 0.05f, fill.g * 0.06f, fill.b * 0.10f, 0.28f}, 16.0f);
        l.drop_shadow({0.0f, 58.0f}, {fill.r * 0.03f, fill.g * 0.04f, fill.b * 0.08f, 0.10f}, 92.0f);
        l.glow(blur, 0.34f, glow, 0.08f, 0.88f, 0.64f);
        l.rounded_rect("bg", {
            .size = size,
            .radius = radius,
            .color = fill,
            .pos = {0.0f, 0.0f, 0.0f},
            .fill = Fill::linear(
                {0.0f, 0.0f},
                {0.0f, 1.0f},
                {
                    {0.0f, {fill.r + 0.03f, fill.g + 0.03f, fill.b + 0.03f, fill.a}},
                    {1.0f, {fill.r - 0.03f, fill.g - 0.02f, fill.b + 0.02f, fill.a}},
                }
            )
        });
        l.rounded_rect("rim", {
            .size = {size.x - 16.0f, 8.0f},
            .radius = 4.0f,
            .color = {1.0f, 1.0f, 1.0f, 0.12f},
            .pos = {0.0f, -size.y * 0.5f + 16.0f, 0.0f},
        });
    });
}

void add_bar_graph(LayerBuilder& l, Vec2 origin, f32 width, f32 base_y, Color color) {
    const f32 bw = width / 5.0f;
    for (int i = 0; i < 4; ++i) {
        const f32 h = 26.0f + i * 16.0f;
        l.rounded_rect("bar_" + std::to_string(i), {
            .size = {bw * 0.58f, h},
            .radius = 4.0f,
            .color = color,
            .pos = {origin.x + i * bw, origin.y - h * 0.5f, 0.0f},
        });
    }
    l.line("axis", {
        .from = {origin.x - 6.0f, base_y, 0.0f},
        .to = {origin.x + width + 2.0f, base_y, 0.0f},
        .thickness = 2.0f,
        .color = {color.r, color.g, color.b, 0.45f},
    });
}

} // namespace

Composition floating_cards_test() {
    return composition({.name = "FloatingCardsTest", .width = kW, .height = kH, .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        s.camera().set(camera_motion::push_in_tilt(p, {.from_z = -900.0f, .to_z = -720.0f, .from_tilt = -12.0f, .to_tilt = 5.0f, .zoom = 880.0f}));
        s.camera().fov(38.0f);
        apply_cinematic_lighting(s,
            {0.16f, 0.18f, 0.28f, 1.0f}, 0.22f,
            {-0.55f, 1.0f, -0.45f}, {0.92f, 0.96f, 1.0f, 1.0f}, 1.10f
        );
        deep_bg(s, Color{0.015f, 0.020f, 0.060f, 1.0f}, Color{0.030f, 0.030f, 0.090f, 1.0f});
        s.layer("ambient", [](LayerBuilder& l) {
            l.position({0.0f, -180.0f, 0.0f});
            l.glow(220.0f, 0.7f, {0.24f, 0.42f, 1.0f, 1.0f});
            l.circle("c", {.radius = 220.0f, .color = {0.12f, 0.20f, 0.60f, 0.16f}, .pos = {-320.0f, 0.0f, 0.0f}});
            l.circle("c2", {.radius = 190.0f, .color = {0.82f, 0.22f, 1.0f, 0.12f}, .pos = {300.0f, 40.0f, 0.0f}});
        });
        add_header(s, "05", "FLOATING CARDS TEST", "Card UI fluttuanti nello spazio.");
        add_neon_floor(s, {0.30f, 0.55f, 1.0f, 0.10f}, 78.0f);

        s.layer("panel", [=](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 8.0f, 12.0f});
            apply_depth_material(l, true, true, 0.80f, 0.16f, 44.0f, 0.64f);
            l.rounded_rect("bg", {
                .size = {1180.0f, 560.0f},
                .radius = 28.0f,
                .color = {0.06f, 0.07f, 0.14f, 0.66f},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 1.0f},
                    {
                        {0.0f, {0.08f, 0.09f, 0.18f, 0.78f}},
                        {1.0f, {0.03f, 0.05f, 0.10f, 0.76f}},
                    }
                )
            });
            l.drop_shadow({0.0f, 26.0f}, {0.0f, 0.0f, 0.0f, 0.58f}, 48.0f);
        });

        add_card(s, "analytics", {-320.0f, 30.0f, 95.0f}, {0.0f, -10.0f, 0.0f}, {280.0f, 300.0f}, 22.0f, {0.06f, 0.22f, 0.44f, 0.94f}, {0.12f, 0.52f, 1.0f, 1.0f}, 20.0f);
        add_card(s, "performance", {0.0f, 0.0f, -36.0f}, {0.0f, 0.0f, 0.0f}, {340.0f, 390.0f}, 24.0f, {0.18f, 0.08f, 0.34f, 0.95f}, {0.76f, 0.34f, 1.0f, 1.0f}, 26.0f);
        add_card(s, "users", {310.0f, 18.0f, 80.0f}, {0.0f, 10.0f, 0.0f}, {250.0f, 300.0f}, 22.0f, {0.10f, 0.10f, 0.22f, 0.94f}, {0.32f, 0.42f, 1.0f, 1.0f}, 18.0f);

        s.layer("analytics_content", [=](LayerBuilder& l) {
            l.enable_3d().position({-320.0f, 30.0f, 80.0f}).rotate({0.0f, -10.0f, 0.0f});
            apply_depth_material(l, false, true, 0.88f, 0.16f, 42.0f, 0.84f);
            l.text("title", {
                .text = "Analytics",
                .size = {220.0f, 40.0f},
                .pos = {0.0f, -108.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_size = 24.0f,
                .color = kWhite,
                .align = TextAlign::Left,
                .vertical_align = VerticalAlign::Middle,
            });
            l.text("sub", {
                .text = "Real-time data",
                .size = {220.0f, 28.0f},
                .pos = {0.0f, -70.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Regular.ttf",
                .font_family = "Inter",
                .font_weight = 400,
                .font_size = 15.0f,
                .color = {0.72f, 0.82f, 0.96f, 1.0f},
                .align = TextAlign::Left,
                .vertical_align = VerticalAlign::Middle,
            });
            add_bar_graph(l, {-98.0f, 72.0f}, 170.0f, 120.0f, {0.28f, 0.72f, 1.0f, 0.88f});
        });

        s.layer("performance_content", [=](LayerBuilder& l) {
            l.enable_3d().position({0.0f, 0.0f, 0.0f});
            apply_depth_material(l, false, true, 0.90f, 0.18f, 48.0f, 0.82f);
            l.text("title", {
                .text = "Performance",
                .size = {260.0f, 44.0f},
                .pos = {0.0f, -120.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_size = 28.0f,
                .color = kWhite,
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
            });
            l.text("pct", {
                .text = "98.6%",
                .size = {260.0f, 80.0f},
                .pos = {0.0f, -24.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_size = 56.0f,
                .color = kWhite,
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
            });
            l.text("small", {
                .text = "Successful",
                .size = {260.0f, 30.0f},
                .pos = {0.0f, 34.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Regular.ttf",
                .font_family = "Inter",
                .font_weight = 400,
                .font_size = 18.0f,
                .color = {0.82f, 0.80f, 0.98f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
            });
            add_bar_graph(l, {-115.0f, 112.0f}, 220.0f, 152.0f, {0.72f, 0.40f, 1.0f, 0.92f});
            l.rounded_rect("btn", {
                .size = {122.0f, 34.0f},
                .radius = 16.0f,
                .color = {0.42f, 0.14f, 0.72f, 1.0f},
                .pos = {0.0f, 130.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 0.0f},
                    {
                        {0.0f, {0.40f, 0.14f, 0.72f, 1.0f}},
                        {1.0f, {0.68f, 0.26f, 0.95f, 1.0f}},
                    }
                )
            });
            l.text("btn_txt", {
                .text = "View Report",
                .size = {122.0f, 34.0f},
                .pos = {0.0f, 130.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 800,
                .font_size = 14.0f,
                .color = kWhite,
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
            });
        });

        s.layer("users_content", [=](LayerBuilder& l) {
            l.enable_3d().position({310.0f, 18.0f, 75.0f}).rotate({0.0f, 10.0f, 0.0f});
            apply_depth_material(l, false, true, 0.86f, 0.16f, 40.0f, 0.82f);
            l.text("title", {
                .text = "Users",
                .size = {190.0f, 36.0f},
                .pos = {0.0f, -110.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_size = 22.0f,
                .color = {0.92f, 0.94f, 1.0f, 1.0f},
                .align = TextAlign::Left,
                .vertical_align = VerticalAlign::Middle,
            });
            l.text("count", {
                .text = "12.6K",
                .size = {190.0f, 56.0f},
                .pos = {0.0f, -58.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_size = 38.0f,
                .color = kWhite,
                .align = TextAlign::Left,
                .vertical_align = VerticalAlign::Middle,
            });
            l.text("delta", {
                .text = "+18.2%",
                .size = {190.0f, 28.0f},
                .pos = {0.0f, -12.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 800,
                .font_size = 16.0f,
                .color = {0.22f, 0.92f, 0.76f, 1.0f},
                .align = TextAlign::Left,
                .vertical_align = VerticalAlign::Middle,
            });
            add_bar_graph(l, {-82.0f, 76.0f}, 155.0f, 108.0f, {0.22f, 0.62f, 1.0f, 0.78f});
        });

        return s.build();
    });
}

Composition orbit_camera_test() {
    return composition({.name = "OrbitCameraTest", .width = kW, .height = kH, .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        s.camera().set(camera_motion::orbit_small(p, 840.0f));
        s.camera().fov(36.0f);
        apply_cinematic_lighting(s,
            {0.12f, 0.14f, 0.22f, 1.0f}, 0.18f,
            {-0.45f, 1.0f, -0.30f}, {0.80f, 0.90f, 1.0f, 1.0f}, 1.15f
        );
        deep_bg(s, Color{0.010f, 0.012f, 0.050f, 1.0f}, Color{0.018f, 0.018f, 0.090f, 1.0f});
        add_header(s, "06", "ORBIT CAMERA TEST", "Camera che orbita attorno al soggetto.");
        add_neon_floor(s, {0.32f, 0.18f, 1.0f, 0.18f}, 88.0f);

        s.layer("top_light", [](LayerBuilder& l) {
            l.position({0.0f, -220.0f, 0.0f});
            l.glow(260.0f, 0.85f, {0.18f, 0.60f, 1.0f, 1.0f});
            l.circle("c", {.radius = 220.0f, .color = {0.18f, 0.60f, 1.0f, 0.22f}, .pos = {0.0f, 0.0f, 0.0f}});
        });

        s.layer("stage", [](LayerBuilder& l) {
            l.position({0.0f, 190.0f, 0.0f});
            l.drop_shadow({0.0f, 18.0f}, {0.04f, 0.02f, 0.16f, 0.50f}, 24.0f);
            l.glow(24.0f, 0.56f, {0.60f, 0.26f, 1.0f, 1.0f});
            l.rounded_rect("base", {
                .size = {480.0f, 62.0f},
                .radius = 31.0f,
                .color = {0.22f, 0.09f, 0.40f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 0.0f},
                    {
                        {0.0f, {0.48f, 0.14f, 0.86f, 1.0f}},
                        {0.5f, {0.72f, 0.24f, 1.0f, 1.0f}},
                        {1.0f, {0.28f, 0.10f, 0.52f, 1.0f}},
                    }
                )
            });
        });

        s.layer("orbits", [=](LayerBuilder& l) {
            l.position({0.0f, -28.0f, 0.0f});
            for (int i = 0; i < 8; ++i) {
                const f32 a = p * 6.28318f + i * 0.785398f;
                const f32 x = std::cos(a) * 320.0f;
                const f32 y = std::sin(a) * 110.0f;
                l.enable_3d().position({x, y, 180.0f + std::sin(a * 1.5f) * 20.0f}).rotate({0.0f, a * 10.0f, 0.0f});
                apply_depth_material(l, false, true, 0.72f, 0.20f, 32.0f, 0.78f);
                l.circle("orb_" + std::to_string(i), {
                    .radius = 20.0f + (i % 3) * 3.0f,
                    .color = {0.52f, 0.42f + 0.06f * i, 1.0f, 0.92f},
                    .pos = {0.0f, 0.0f, 0.0f},
                });
                l.glow(12.0f, 0.85f, {0.60f, 0.44f, 1.0f, 1.0f});
            }
        });

        s.layer("title", [=](LayerBuilder& l) {
            l.position({0.0f, -30.0f, 0.0f});
            l.enable_3d().rotate({4.0f, 0.0f, 0.0f});
            apply_depth_material(l, false, true, 0.92f, 0.20f, 58.0f, 0.72f);
            l.glow(28.0f, 0.74f, {0.78f, 0.62f, 1.0f, 1.0f});
            l.drop_shadow({0.0f, 20.0f}, {0.06f, 0.00f, 0.22f, 0.76f}, 22.0f);
            l.text("create", {
                .text = "CREATE",
                .size = {1080.0f, 140.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_size = 112.0f,
                .color = {0.98f, 0.95f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 1.0f,
                .paint = {
                    .fill = {0.98f, 0.95f, 1.0f, 1.0f},
                    .fill_style = Fill::linear(
                        {0.0f, 0.0f},
                        {0.0f, 1.0f},
                        {
                            {0.0f, {1.0f, 1.0f, 1.0f, 1.0f}},
                            {0.55f, {0.80f, 0.88f, 1.0f, 1.0f}},
                            {1.0f, {0.56f, 0.70f, 1.0f, 1.0f}},
                        }
                    ),
                    .stroke_enabled = true,
                    .stroke_color = {0.10f, 0.22f, 0.50f, 0.72f},
                    .stroke_width = 2.0f,
                },
            });
            l.text("sub", {
                .text = "WITHOUT LIMITS",
                .size = {720.0f, 48.0f},
                .pos = {0.0f, 94.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 800,
                .font_size = 28.0f,
                .color = {0.76f, 0.74f, 0.98f, 0.92f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 8.0f,
                .paint = {
                    .fill = {0.76f, 0.74f, 0.98f, 0.92f},
                    .stroke_enabled = true,
                    .stroke_color = {0.10f, 0.10f, 0.22f, 0.28f},
                    .stroke_width = 1.5f,
                },
            });
        });

        return s.build();
    });
}

Composition depth_fog_test() {
    return composition({.name = "DepthFogTest", .width = kW, .height = kH, .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        s.camera().set(camera_motion::push_in_tilt(p, {.from_z = -940.0f, .to_z = -640.0f, .from_tilt = -10.0f, .to_tilt = 3.0f, .zoom = 900.0f}));
        s.camera().fov(40.0f);
        apply_cinematic_lighting(s,
            {0.10f, 0.14f, 0.24f, 1.0f}, 0.24f,
            {-0.35f, 0.95f, -0.55f}, {0.92f, 0.98f, 1.0f, 1.0f}, 1.00f
        );
        deep_bg(s, Color{0.015f, 0.025f, 0.055f, 1.0f}, Color{0.005f, 0.010f, 0.030f, 1.0f});
        add_header(s, "07", "DEPTH FOG TEST", "Verifica della foschia per profondità.");

        s.layer("fog", [](LayerBuilder& l) {
            l.position({0.0f, 160.0f, 0.0f});
            l.opacity(0.82f);
            l.glow(90.0f, 0.9f, {0.26f, 0.62f, 1.0f, 1.0f});
            l.circle("mist", {.radius = 280.0f, .color = {0.10f, 0.40f, 1.0f, 0.16f}, .pos = {0.0f, 0.0f, 0.0f}});
        });

        s.layer("mountains_back", [](LayerBuilder& l) {
            l.position({0.0f, 120.0f, 180.0f});
            l.opacity(0.86f);
            l.rounded_rect("left", {.size = {420.0f, 260.0f}, .radius = 22.0f, .color = {0.04f, 0.08f, 0.16f, 1.0f}, .pos = {-420.0f, 60.0f, 0.0f}});
            l.rounded_rect("center", {.size = {620.0f, 360.0f}, .radius = 26.0f, .color = {0.05f, 0.10f, 0.20f, 1.0f}, .pos = {0.0f, -20.0f, 0.0f}});
            l.rounded_rect("right", {.size = {360.0f, 220.0f}, .radius = 20.0f, .color = {0.04f, 0.08f, 0.16f, 1.0f}, .pos = {430.0f, 70.0f, 0.0f}});
        });

        s.layer("mountains_mid", [](LayerBuilder& l) {
            l.position({0.0f, 180.0f, 20.0f});
            l.opacity(0.92f);
            l.rounded_rect("left", {.size = {500.0f, 300.0f}, .radius = 28.0f, .color = {0.05f, 0.11f, 0.24f, 1.0f}, .pos = {-520.0f, 55.0f, 0.0f}});
            l.rounded_rect("center", {.size = {760.0f, 430.0f}, .radius = 34.0f, .color = {0.08f, 0.14f, 0.30f, 1.0f}, .pos = {0.0f, -10.0f, 0.0f}});
            l.rounded_rect("right", {.size = {460.0f, 260.0f}, .radius = 24.0f, .color = {0.05f, 0.10f, 0.22f, 1.0f}, .pos = {520.0f, 70.0f, 0.0f}});
            l.glow(44.0f, 0.75f, {0.14f, 0.42f, 1.0f, 1.0f});
        });

        s.layer("ground", [](LayerBuilder& l) {
            l.position({0.0f, 360.0f, 0.0f});
            l.rect("floor", {
                .size = {1500.0f, 240.0f},
                .color = {0.03f, 0.05f, 0.12f, 1.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {0.0f, 1.0f},
                    {
                        {0.0f, {0.03f, 0.05f, 0.14f, 0.0f}},
                        {1.0f, {0.03f, 0.05f, 0.12f, 0.90f}},
                    }
                )
            });
            l.glow(36.0f, 0.8f, {0.10f, 0.46f, 1.0f, 1.0f});
        });

        s.layer("title", [](LayerBuilder& l) {
            l.position({0.0f, -10.0f, 0.0f});
            l.text("future", {
                .text = "FUTURE",
                .size = {700.0f, 80.0f},
                .pos = {0.0f, -78.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_size = 44.0f,
                .color = {0.26f, 0.48f, 0.80f, 0.36f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
            });
            l.glow(40.0f, 1.2f, {0.22f, 0.72f, 1.0f, 1.0f});
            l.text("innovate", {
                .text = "INNOVATE",
                .size = {780.0f, 96.0f},
                .pos = {0.0f, -8.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_size = 56.0f,
                .color = {0.40f, 0.64f, 0.92f, 0.52f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
            });
            l.text("build", {
                .text = "BUILD",
                .size = {920.0f, 150.0f},
                .pos = {0.0f, 104.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_size = 94.0f,
                .color = {0.82f, 0.93f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 0.0f,
            });
        });

        return s.build();
    });
}

Composition z_stack_parallax_test() {
    return composition({.name = "ZStackParallaxTest", .width = kW, .height = kH, .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        s.camera().set(camera_motion::parallax_sweep(p, 120.0f, -1100.0f, 850.0f));
        s.camera().fov(38.0f);
        apply_cinematic_lighting(s,
            {0.12f, 0.16f, 0.28f, 1.0f}, 0.20f,
            {-0.50f, 0.90f, -0.55f}, {0.88f, 0.95f, 1.0f, 1.0f}, 1.05f
        );
        deep_bg(s, Color{0.010f, 0.020f, 0.060f, 1.0f}, Color{0.020f, 0.020f, 0.100f, 1.0f});
        add_header(s, "04", "Z-STACK PARALLAX TEST", "Più layer su profondità diverse.");
        add_neon_floor(s, {0.22f, 0.72f, 1.0f, 0.10f}, 76.0f);

        auto card = [&](const std::string& id, Vec3 pos, Vec3 rot, Vec2 size, Color fill, Color glow, const std::string& label, const std::string& ztxt) {
            s.layer("card_" + id, [=](LayerBuilder& l) {
                l.enable_3d().position(pos).rotate(rot);
                apply_depth_material(l, true, true, 0.80f, 0.16f, 46.0f, 0.72f);
                l.drop_shadow({0.0f, 20.0f}, {0.03f, 0.03f, 0.08f, 0.28f}, 18.0f);
                l.glow(16.0f, 0.34f, glow);
                l.rounded_rect("bg", {
                    .size = size,
                    .radius = 18.0f,
                    .color = fill,
                    .pos = {0.0f, 0.0f, 0.0f},
                    .fill = Fill::linear(
                        {0.0f, 0.0f},
                        {0.0f, 1.0f},
                        {
                            {0.0f, {fill.r + 0.05f, fill.g + 0.05f, fill.b + 0.08f, 1.0f}},
                            {1.0f, {fill.r - 0.04f, fill.g - 0.02f, fill.b + 0.02f, 1.0f}},
                        }
                    )
                });
                l.text("label", {
                    .text = label,
                    .size = {size.x, 54.0f},
                    .pos = {0.0f, -18.0f, 0.0f},
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .font_family = "Inter",
                    .font_weight = 900,
                    .font_size = 30.0f,
                    .color = kWhite,
                    .align = TextAlign::Center,
                    .vertical_align = VerticalAlign::Middle,
                });
                l.text("z", {
                    .text = ztxt,
                    .size = {size.x, 42.0f},
                    .pos = {0.0f, 32.0f, 0.0f},
                    .font_path = "assets/fonts/Inter-Bold.ttf",
                    .font_family = "Inter",
                    .font_weight = 800,
                    .font_size = 18.0f,
                    .color = {0.82f, 0.88f, 1.0f, 0.80f},
                    .align = TextAlign::Center,
                    .vertical_align = VerticalAlign::Middle,
                });
            });
        };

        card("back", {-300.0f, 8.0f, 150.0f}, {0.0f, -8.0f, 0.0f}, {280.0f, 320.0f}, {0.05f, 0.10f, 0.26f, 0.90f}, {0.18f, 0.30f, 1.0f, 1.0f}, "BACK", "Z -400");
        card("mid", {0.0f, -6.0f, 20.0f}, {0.0f, 0.0f, 0.0f}, {320.0f, 360.0f}, {0.08f, 0.14f, 0.34f, 0.94f}, {0.42f, 0.64f, 1.0f, 1.0f}, "MID", "Z 0");
        card("front", {300.0f, 12.0f, -160.0f}, {0.0f, 8.0f, 0.0f}, {340.0f, 380.0f}, {0.04f, 0.26f, 0.34f, 0.94f}, {0.22f, 0.90f, 1.0f, 1.0f}, "FRONT", "Z 250");

        return s.build();
    });
}

Composition shadow_glow_consistency_test() {
    return composition({.name = "ShadowGlowConsistencyTest", .width = kW, .height = kH, .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        s.camera().set(camera_motion::parallax_sweep(p, 90.0f, -1000.0f, 820.0f));
        s.camera().fov(40.0f);
        apply_cinematic_lighting(s,
            {0.14f, 0.08f, 0.20f, 1.0f}, 0.18f,
            {-0.42f, 0.95f, -0.36f}, {0.92f, 0.88f, 1.0f, 1.0f}, 1.08f
        );
        deep_bg(s, Color{0.028f, 0.008f, 0.050f, 1.0f}, Color{0.050f, 0.016f, 0.090f, 1.0f});
        add_header(s, "09", "SHADOW + GLOW CONSISTENCY TEST", "Verifica coerenza ombre e glow in movimento.");
        add_neon_floor(s, {0.72f, 0.18f, 1.0f, 0.16f}, 84.0f);

        s.layer("panel", [](LayerBuilder& l) {
            l.position({0.0f, 70.0f, 0.0f});
            apply_depth_material(l, true, true, 0.80f, 0.18f, 44.0f, 0.64f);
            l.rounded_rect("bg", {
                .size = {1060.0f, 420.0f},
                .radius = 28.0f,
                .color = {0.10f, 0.06f, 0.18f, 0.82f},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {0.0f, 1.0f},
                    {
                        {0.0f, {0.16f, 0.06f, 0.24f, 0.92f}},
                        {1.0f, {0.06f, 0.03f, 0.12f, 0.86f}},
                    }
                )
            });
            l.drop_shadow({0.0f, 30.0f}, {0.0f, 0.0f, 0.0f, 0.40f}, 32.0f);
            l.glow(14.0f, 0.28f, {0.82f, 0.20f, 1.0f, 1.0f});
        });

        s.layer("left_card", [](LayerBuilder& l) {
            l.position({-330.0f, 60.0f, 40.0f}).rotate({0.0f, -8.0f, 0.0f});
            apply_depth_material(l, true, true, 0.78f, 0.14f, 40.0f, 0.70f);
            l.rounded_rect("bg", {
                .size = {220.0f, 300.0f},
                .radius = 18.0f,
                .color = {0.12f, 0.10f, 0.20f, 0.94f},
                .pos = {0.0f, 0.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {0.0f, 1.0f},
                    {
                        {0.0f, {0.16f, 0.12f, 0.26f, 0.98f}},
                        {1.0f, {0.06f, 0.05f, 0.12f, 0.96f}},
                    }
                )
            });
            l.drop_shadow({0.0f, 20.0f}, {0.04f, 0.02f, 0.08f, 0.40f}, 18.0f);
            l.glow(10.0f, 0.20f, {0.82f, 0.20f, 1.0f, 1.0f});
            l.text("title", {
                .text = "Automate\nWorkflows",
                .size = {180.0f, 120.0f},
                .pos = {0.0f, -70.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_size = 28.0f,
                .color = kWhite,
                .align = TextAlign::Left,
                .vertical_align = VerticalAlign::Middle,
                .line_height = 1.0f,
                .paint = {
                    .fill = {1.0f, 0.96f, 1.0f, 1.0f},
                    .stroke_enabled = true,
                    .stroke_color = {0.10f, 0.02f, 0.20f, 0.35f},
                    .stroke_width = 1.25f,
                },
            });
            l.rounded_rect("btn", {
                .size = {118.0f, 30.0f},
                .radius = 14.0f,
                .color = {0.32f, 0.14f, 0.72f, 1.0f},
                .pos = {0.0f, 104.0f, 0.0f},
            });
            l.text("btn_txt", {
                .text = "Get Started",
                .size = {118.0f, 30.0f},
                .pos = {0.0f, 104.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 800,
                .font_size = 14.0f,
                .color = kWhite,
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
            });
        });

        s.layer("hero_text", [](LayerBuilder& l) {
            l.position({300.0f, -8.0f, -40.0f}).rotate({3.0f, -4.0f, 0.0f});
            apply_depth_material(l, false, true, 0.88f, 0.22f, 52.0f, 0.78f);
            l.glow(18.0f, 0.42f, {0.82f, 0.42f, 1.0f, 1.0f});
            l.drop_shadow({0.0f, 20.0f}, {0.06f, 0.02f, 0.18f, 0.58f}, 18.0f);
            l.accepts_lights(false);
            l.text("text", {
                .text = "FASTER\nSMARTER\nBETTER",
                .size = {520.0f, 360.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_size = 70.0f,
                .color = {0.98f, 0.92f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .line_height = 0.92f,
                .tracking = 0.5f,
                .paint = {
                    .fill = {0.98f, 0.92f, 1.0f, 1.0f},
                    .fill_style = Fill::linear(
                        {0.0f, 0.0f},
                        {0.0f, 1.0f},
                        {
                            {0.0f, {1.0f, 0.98f, 1.0f, 1.0f}},
                            {0.60f, {0.88f, 0.72f, 1.0f, 1.0f}},
                            {1.0f, {0.72f, 0.40f, 1.0f, 1.0f}},
                        }
                    ),
                    .stroke_enabled = true,
                    .stroke_color = {0.22f, 0.02f, 0.34f, 0.62f},
                    .stroke_width = 1.8f,
                },
            });
        });

        return s.build();
    });
}

Composition extreme_perspective_test() {
    return composition({.name = "ExtremePerspectiveTest", .width = kW, .height = kH, .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        s.camera().set(camera_motion::orbit_small(p, 1020.0f));
        s.camera().fov(34.0f);
        apply_cinematic_lighting(s,
            {0.10f, 0.16f, 0.28f, 1.0f}, 0.20f,
            {-0.35f, 1.0f, -0.50f}, {0.86f, 0.96f, 1.0f, 1.0f}, 1.15f
        );
        deep_bg(s, Color{0.012f, 0.030f, 0.090f, 1.0f}, Color{0.020f, 0.040f, 0.140f, 1.0f});
        add_header(s, "03", "EXTREME PERSPECTIVE TEST", "Prospettiva estrema per stressare il sistema.");
        add_neon_floor(s, {0.22f, 0.68f, 1.0f, 0.15f}, 72.0f);

        s.layer("spot", [](LayerBuilder& l) {
            l.position({0.0f, -250.0f, 0.0f});
            l.glow(220.0f, 1.0f, {0.18f, 0.72f, 1.0f, 1.0f});
            l.circle("c", {.radius = 180.0f, .color = {0.22f, 0.62f, 1.0f, 0.28f}, .pos = {0.0f, 0.0f, 0.0f}});
        });

        s.layer("title", [=](LayerBuilder& l) {
            l.enable_3d().position({-100.0f, 40.0f, -40.0f}).rotate({0.0f, -22.0f, 0.0f});
            apply_depth_material(l, false, true, 0.92f, 0.22f, 56.0f, 0.78f);
            l.glow(18.0f, 0.45f, {0.42f, 0.76f, 1.0f, 1.0f});
            l.drop_shadow({0.0f, 20.0f}, {0.02f, 0.10f, 0.25f, 0.68f}, 24.0f);
            l.text("txt", {
                .text = "MASTERCLASS",
                .size = {1080.0f, 160.0f},
                .pos = {0.0f, 0.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_size = 104.0f,
                .color = {0.94f, 0.97f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = -2.0f,
                .paint = {
                    .fill = {0.94f, 0.97f, 1.0f, 1.0f},
                    .fill_style = Fill::linear(
                        {0.0f, 0.0f},
                        {0.0f, 1.0f},
                        {
                            {0.0f, {1.0f, 1.0f, 1.0f, 1.0f}},
                            {0.55f, {0.82f, 0.90f, 1.0f, 1.0f}},
                            {1.0f, {0.50f, 0.70f, 1.0f, 1.0f}},
                        }
                    ),
                    .stroke_enabled = true,
                    .stroke_color = {0.18f, 0.28f, 0.58f, 0.55f},
                    .stroke_width = 2.0f,
                },
            });
        });

        return s.build();
    });
}

Composition y_rotation_text_test() {
    return composition({.name = "YRotationTextTest", .width = kW, .height = kH, .duration = 180}, [](const FrameContext& ctx) {
        SceneBuilder s(ctx);
        const f32 p = ctx.progress();
        s.camera().set(camera_motion::orbit_small(p, 880.0f));
        s.camera().fov(44.0f);
        apply_cinematic_lighting(s,
            {0.12f, 0.10f, 0.20f, 1.0f}, 0.24f,
            {-0.40f, 1.0f, -0.30f}, {0.92f, 0.82f, 1.0f, 1.0f}, 1.00f
        );
        deep_bg(s, Color{0.012f, 0.010f, 0.045f, 1.0f}, Color{0.050f, 0.015f, 0.110f, 1.0f});
        add_header(s, "02", "Y-ROTATION TEXT TEST", "Test rotazione sull'asse Y.");
        add_neon_floor(s, {0.78f, 0.20f, 1.0f, 0.18f}, 84.0f);

        s.layer("panel", [](LayerBuilder& l) {
            l.position({0.0f, 20.0f, 0.0f});
            apply_depth_material(l, true, true, 0.78f, 0.18f, 42.0f, 0.68f);
            l.rounded_rect("bg", {
                .size = {920.0f, 360.0f},
                .radius = 28.0f,
                .color = {0.16f, 0.08f, 0.26f, 0.74f},
                .pos = {0.0f, 0.0f, 0.0f},
            });
            l.glow(24.0f, 0.9f, {0.76f, 0.34f, 1.0f, 1.0f});
        });

        s.layer("hero", [=](LayerBuilder& l) {
            l.enable_3d().position({-20.0f, -6.0f, -30.0f}).rotate({3.5f, 10.0f + std::sin(p * 6.28318f) * 8.0f, 0.0f});
            apply_depth_material(l, false, true, 0.90f, 0.20f, 54.0f, 0.76f);
            l.glow(20.0f, 0.55f, {0.82f, 0.42f, 1.0f, 1.0f});
            l.drop_shadow({0.0f, 20.0f}, {0.08f, 0.02f, 0.24f, 0.74f}, 20.0f);
            l.text("buttery", {
                .text = "BUTTERY",
                .size = {700.0f, 120.0f},
                .pos = {0.0f, -24.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 900,
                .font_size = 92.0f,
                .color = {0.98f, 0.76f, 1.0f, 1.0f},
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = -3.0f,
                .paint = {
                    .fill = {0.98f, 0.76f, 1.0f, 1.0f},
                    .fill_style = Fill::linear(
                        {0.0f, 0.0f},
                        {0.0f, 1.0f},
                        {
                            {0.0f, {1.0f, 0.94f, 1.0f, 1.0f}},
                            {0.55f, {0.94f, 0.58f, 1.0f, 1.0f}},
                            {1.0f, {0.72f, 0.22f, 1.0f, 1.0f}},
                        }
                    ),
                    .stroke_enabled = true,
                    .stroke_color = {0.24f, 0.02f, 0.30f, 0.56f},
                    .stroke_width = 1.8f,
                },
            });
            l.rounded_rect("pill", {
                .size = {300.0f, 56.0f},
                .radius = 18.0f,
                .color = {0.20f, 0.12f, 0.38f, 1.0f},
                .pos = {0.0f, 112.0f, 0.0f},
                .fill = Fill::linear(
                    {0.0f, 0.0f},
                    {1.0f, 0.0f},
                    {
                        {0.0f, {0.30f, 0.14f, 0.72f, 1.0f}},
                        {1.0f, {0.56f, 0.22f, 0.92f, 1.0f}},
                    }
                )
            });
            l.text("pill_txt", {
                .text = "SMOOTH ANIMATION",
                .size = {300.0f, 56.0f},
                .pos = {0.0f, 112.0f, 0.0f},
                .font_path = "assets/fonts/Inter-Bold.ttf",
                .font_family = "Inter",
                .font_weight = 800,
                .font_size = 16.0f,
                .color = kWhite,
                .align = TextAlign::Center,
                .vertical_align = VerticalAlign::Middle,
                .tracking = 1.0f,
            });
        });

        return s.build();
    });
}

} // namespace chronon3d::content::effects

CHRONON_REGISTER_COMPOSITION("FloatingCardsTest", chronon3d::content::effects::floating_cards_test)
CHRONON_REGISTER_COMPOSITION("OrbitCameraTest", chronon3d::content::effects::orbit_camera_test)
CHRONON_REGISTER_COMPOSITION("DepthFogTest", chronon3d::content::effects::depth_fog_test)
CHRONON_REGISTER_COMPOSITION("ZStackParallaxTest", chronon3d::content::effects::z_stack_parallax_test)
CHRONON_REGISTER_COMPOSITION("ShadowGlowConsistencyTest", chronon3d::content::effects::shadow_glow_consistency_test)
CHRONON_REGISTER_COMPOSITION("ExtremePerspectiveTest", chronon3d::content::effects::extreme_perspective_test)
CHRONON_REGISTER_COMPOSITION("YRotationTextTest", chronon3d::content::effects::y_rotation_text_test)

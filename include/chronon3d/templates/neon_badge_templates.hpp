#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <cmath>

namespace chronon3d {
namespace templates {

// ─── dark_grid_background ────────────────────────────────────────────────────
// Flat dark background with subtle crosshatch grid.
// Sets up a front-facing Camera2_5D internally; call before any 3D layers.

inline void dark_grid_background(SceneBuilder& s, const FrameContext& ctx,
                                  const DarkGridBgParams& p = {}) {
    const f32 H = static_cast<f32>(ctx.height > 0 ? ctx.height : 720);

    Camera2_5D cam;
    cam.enabled                    = true;
    cam.position                   = {0.f, 0.f, -H};
    cam.point_of_interest          = {0.f, 0.f, 0.f};
    cam.point_of_interest_enabled  = true;
    cam.zoom                       = H;
    s.camera_2_5d(cam);

    s.layer("nbg_bg", [&p](LayerBuilder& l) {
        l.rect("r", {.size = {99999.f, 99999.f}, .color = p.bg_color});
    });

    s.layer("nbg_grid", [&p](LayerBuilder& l) {
        l.enable_3d().position({0.f, 0.f, 0.f}).grid_plane("g", {
            .pos           = {0.f, 0.f, 0.f},
            .axis          = PlaneAxis::XY,
            .extent        = p.extent,
            .spacing       = p.spacing,
            .color         = p.grid_color,
            .fade_distance = 0.0f,
        });
    });
}

// ─── NeonBadgeParams ─────────────────────────────────────────────────────────

struct NeonBadgeParams {
    std::string text     {"TEXT"};
    std::string font_path{"assets/fonts/Inter-Bold.ttf"};
    f32         font_size{200.f};
    Color       text_color{1.f, 1.f, 1.f, 1.f};

    // Red box
    Color box_color  {0.86f, 0.08f, 0.12f, 1.f};
    f32   box_width  {680.f};
    Vec2  box_padding{0.f,  24.f};
    f32   box_depth  {2.f};

    // Glow (white halo around the box, pulsing)
    Color glow_color    {1.f, 1.f, 1.f, 1.f};
    f32   glow_radius   {40.f};
    f32   glow_base     {0.55f};
    f32   glow_amplitude{0.25f};
    f32   glow_speed    {1.8f};

    // 3D tilt
    bool  enable_tilt{true};
    f32   tilt_x_amp {5.f};
    f32   tilt_y_amp {4.f};
    f32   tilt_z_amp {2.f};

    // Decorative bars (top + bottom, wipe-in easeOutBack)
    bool  enable_bars      {true};
    f32   bar_height       {22.f};
    f32   bar_wipe_duration{0.6f};
};

// ─── neon_badge ──────────────────────────────────────────────────────────────
// Builds: red box with pulsing glow, optional top/bottom bars, white text.
// All elements share the same XYZ tilt — they move as a rigid group.
// Layer name prefix: "nb_"

inline void neon_badge(SceneBuilder& s, const FrameContext& ctx,
                        const NeonBadgeParams& p = {}) {
    const f32 fps  = static_cast<f32>(ctx.frame_rate.numerator) /
                     static_cast<f32>(ctx.frame_rate.denominator > 0
                                      ? ctx.frame_rate.denominator : 1);
    const f32 time = static_cast<f32>(ctx.frame) / fps;

    const f32 glow_intensity = p.glow_base +
        p.glow_amplitude * std::sin(time * p.glow_speed * 6.28318f);

    const f32 rot_x = p.enable_tilt ? std::sin(time * 1.2f) * p.tilt_x_amp : 0.f;
    const f32 rot_y = p.enable_tilt ? std::cos(time * 0.9f) * p.tilt_y_amp : 0.f;
    const f32 rot_z = p.enable_tilt ? std::sin(time * 0.5f) * p.tilt_z_amp : 0.f;

    const f32 t_bars = std::min(time / std::max(p.bar_wipe_duration, 0.001f), 1.0f);
    auto ease_out_back = [](f32 t) -> f32 {
        constexpr f32 c1 = 1.70158f, c3 = c1 + 1.f;
        return 1.f + c3 * std::pow(t - 1.f, 3.f) + c1 * std::pow(t - 1.f, 2.f);
    };
    const f32 bar_scale = std::max(0.f, ease_out_back(t_bars));

    const f32 box_h = p.font_size * 0.72f + p.box_padding.y * 2.f;

    s.layer("nb_box", [&](LayerBuilder& l) {
        l.enable_3d().position({0.f, 0.f, 0.f}).rotate({rot_x, rot_y, rot_z});
        l.fake_box3d("box", {
            .pos   = {0.f, 0.f, 0.f},
            .size  = {p.box_width, box_h},
            .depth = p.box_depth,
            .color = p.box_color,
        });
        l.with_glow({
            .enabled   = true,
            .radius    = p.glow_radius,
            .intensity = glow_intensity,
            .color     = p.glow_color,
        });
    });

    if (p.enable_bars) {
        const f32 bar_w = p.box_width * bar_scale;
        const f32 bar_y = box_h * 0.5f + p.bar_height * 0.5f + 4.f;

        s.layer("nb_bar_t", [&](LayerBuilder& l) {
            l.enable_3d().position({0.f, 0.f, 0.f}).rotate({rot_x, rot_y, rot_z});
            l.fake_box3d("b", {
                .pos   = {0.f,  bar_y, 0.f},
                .size  = {bar_w, p.bar_height},
                .depth = 2.f,
                .color = p.box_color,
            });
        });

        s.layer("nb_bar_b", [&](LayerBuilder& l) {
            l.enable_3d().position({0.f, 0.f, 0.f}).rotate({rot_x, rot_y, rot_z});
            l.fake_box3d("b", {
                .pos   = {0.f, -bar_y, 0.f},
                .size  = {bar_w, p.bar_height},
                .depth = 2.f,
                .color = p.box_color,
            });
        });
    }

    s.layer("nb_text", [&](LayerBuilder& l) {
        l.enable_3d().position({0.f, 0.f, 0.f}).rotate({rot_x, rot_y, rot_z});
        l.fake_extruded_text("t", {
            .text              = p.text,
            .font_path         = p.font_path,
            .font_size         = p.font_size,
            .depth             = 0,
            .extrude_z_step    = 0.f,
            .front_color       = p.text_color,
            .side_color        = {0.8f, 0.8f, 0.8f, 1.f},
            .bevel_size        = 0.f,
            .highlight_opacity = 0.f,
        });
    });
}

} // namespace templates
} // namespace chronon3d

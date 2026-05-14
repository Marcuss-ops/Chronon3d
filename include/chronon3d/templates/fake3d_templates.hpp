#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/renderer/materials.hpp>
#include <string>

namespace chronon3d {
namespace templates {

// ─────────────────────────────────────────────────────────────────────────────
// Theme: color presets for the studio templates

struct Theme {
    Color primary{Materials::studio_orange()};
    Color accent{Materials::studio_blue()};
    Color background{Materials::studio_background()};
    Color grid{Materials::dark_floor_grid()};
    Color text_front{0.97f, 0.96f, 0.92f, 1.0f};
    Color text_side{0.46f, 0.40f, 0.31f, 0.95f};

    static Theme orange()  { return {}; }
    static Theme blue()    { return {Materials::studio_blue(),   Materials::studio_red()}; }
    static Theme red()     { return {Materials::studio_red(),    Materials::studio_orange()}; }
    static Theme mono()    { return {Color{0.88f,0.88f,0.88f,1}, Color{0.55f,0.55f,0.55f,1}}; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Fake3DTitleStudioParams

struct Fake3DTitleStudioParams {
    std::string  title{"TITLE"};
    std::string  font_path{"assets/fonts/Inter-Bold.ttf"};
    Theme        theme{Theme::orange()};
    f32          font_size{88.0f};

    // Camera
    f32  camera_y{480.0f};
    f32  camera_z{-1100.0f};
    f32  camera_zoom{1020.0f};
    f32  orbit_start_deg{25.0f};  // initial orbit angle
    f32  orbit_total_deg{360.0f}; // degrees over full duration

    // Layout
    Vec3 panel_pos{0, 0, 0};
    Vec2 panel_size{600, 240};
    f32  panel_depth{90.0f};
    Vec3 cube_left_pos{-440, 0, 0};
    Vec3 cube_right_pos{440, 0, 0};
    Vec2 cube_size{165, 165};
    f32  cube_depth{165.0f};

    // Grid
    f32  grid_extent{3000.0f};
    f32  grid_spacing{180.0f};
    f32  grid_fade_distance{2200.0f};

    // Text (flat 2D-in-3D by default — looks premium without fake-extrusion artifacts)
    Vec3 text_pos{0, 240, -120};  // above panel top (panel height ~240), in front
    f32  text_shadow_opacity{0.55f};
    f32  text_glow_radius{10.0f};
    f32  text_glow_intensity{0.22f};
    Color text_glow_color{1.0f, 0.88f, 0.60f, 1.0f};

    // Optional fake extrusion for the title shot; disabled by default.
    bool use_extruded_text{false};
    bool title_is_3d{true};
    f32  text_depth{18};
    f32  text_extrude_z{2.2f};
    f32  text_extrude_x{0.3f};

    bool contact_shadows{true};
    bool bloom{true};
};

// ─────────────────────────────────────────────────────────────────────────────
// Fake3DTitleStudio — main studio template

inline void fake3d_title_studio(SceneBuilder& s, const FrameContext& ctx,
                                const Fake3DTitleStudioParams& p = {}) {
    const f32 t = static_cast<f32>(ctx.frame) / std::max(Frame{1}, ctx.duration);

    // Background
    s.layer("tmpl_bg", [&p](LayerBuilder& l) {
        l.rect("fill", {.size = {99999.0f, 99999.0f}, .color = p.theme.background});
    });

    // Camera orbit rig
    s.null_layer("tmpl_cam_rig", [&](LayerBuilder& l) {
        l.position({0, 0, 0}).rotate({0, p.orbit_start_deg + t * p.orbit_total_deg, 0});
    });
    s.enable_camera_2_5d()
     .camera_position({0, p.camera_y, p.camera_z})
     .camera_zoom(p.camera_zoom)
     .camera_parent("tmpl_cam_rig")
     .camera_look_at({0, 0, 0});

    // Grid with depth fade
    s.grid_plane_layer("tmpl_grid", {
        .pos            = {0, -210, 0},
        .axis           = PlaneAxis::XZ,
        .extent         = p.grid_extent,
        .spacing        = p.grid_spacing,
        .color          = p.theme.grid,
        .fade_distance  = p.grid_fade_distance,
        .fade_min_alpha = 0.0f
    });

    // Cubes
    s.fake_box3d_layer("tmpl_cube_l", {
        .pos   = p.cube_left_pos,  .size = p.cube_size, .depth = p.cube_depth,
        .color = p.theme.accent,   .contact_shadow = p.contact_shadows
    });
    s.fake_box3d_layer("tmpl_cube_r", {
        .pos   = p.cube_right_pos, .size = p.cube_size, .depth = p.cube_depth,
        .color = p.theme.accent,   .contact_shadow = p.contact_shadows
    });

    // Center panel
    s.fake_box3d_layer("tmpl_panel", {
        .pos   = p.panel_pos,  .size = p.panel_size, .depth = p.panel_depth,
        .color = p.theme.primary, .contact_shadow = p.contact_shadows
    });

    // Title text: flat 2D-in-3D (clean, premium) or fake extrusion
    if (p.use_extruded_text) {
        if (p.title_is_3d) {
            s.fake_extruded_text_layer("tmpl_title", {
                .text           = p.title,
                .font_path      = p.font_path,
                .pos            = p.text_pos,
                .font_size      = p.font_size,
                .depth          = static_cast<int>(p.text_depth),
                .extrude_dir    = {p.text_extrude_x, 0.0f},
                .extrude_z_step = p.text_extrude_z,
                .front_color    = p.theme.text_front,
                .side_color     = p.theme.text_side,
                .side_fade      = 0.06f,
                .align          = TextAlign::Center,
                .highlight_opacity = 0.11f
            });
        } else {
            s.layer("tmpl_title", [&p](LayerBuilder& l) {
                l.fake_extruded_text("text", {
                     .text           = p.title,
                     .font_path      = p.font_path,
                     .pos            = p.text_pos,
                     .font_size      = p.font_size,
                     .depth          = static_cast<int>(p.text_depth),
                     .extrude_dir    = {p.text_extrude_x, 0.0f},
                     .extrude_z_step = p.text_extrude_z,
                     .front_color    = p.theme.text_front,
                     .side_color     = p.theme.text_side,
                     .side_fade      = 0.06f,
                     .align          = TextAlign::Center,
                     .highlight_opacity = 0.11f,
                     .bevel_size     = 1.5f
                 });
            });
        }
    } else {
        s.layer("tmpl_title", [&p](LayerBuilder& l) {
            l.enable_3d()
             .position(p.text_pos)
             .text("t", {
                 .content = p.title,
                 .style   = {.font_path = p.font_path, .size = p.font_size,
                             .color = p.theme.text_front, .align = TextAlign::Center}
             })
             .drop_shadow({0, 6}, Color{0, 0, 0, p.text_shadow_opacity}, 14.0f)
             .glow(p.text_glow_radius, p.text_glow_intensity, p.text_glow_color);
        });
    }

    // Bloom
    if (p.bloom) {
        s.adjustment_layer("tmpl_bloom", [](LayerBuilder& l) {
            l.bloom_soft();
        });
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// LowerThird: minimal 2.5D lower third with text + accent bar

struct LowerThirdParams {
    std::string title{"Title"};
    std::string subtitle;
    std::string font_path{"assets/fonts/Inter-Bold.ttf"};
    Color bar_color{Materials::studio_orange()};
    Color title_color{1.0f, 1.0f, 1.0f, 1.0f};
    Color sub_color{0.75f, 0.75f, 0.75f, 1.0f};
    f32 bar_y{580.0f};
    f32 title_size{52.0f};
    f32 sub_size{28.0f};
    Vec2 bar_size{900.0f, 6.0f};
};

inline void lower_third(SceneBuilder& s, const LowerThirdParams& p = {}) {
    // Accent bar
    s.layer("lt_bar", [&p](LayerBuilder& l) {
        l.rect("bar", {
            .size  = p.bar_size,
            .color = p.bar_color,
            .pos   = {640, p.bar_y, 0}
        });
    });

    // Title
    s.layer("lt_title", [&p](LayerBuilder& l) {
        l.text("t", {
            .content = p.title,
            .style   = {.font_path = p.font_path, .size = p.title_size,
                        .color = p.title_color, .align = TextAlign::Left},
            .pos     = {190, p.bar_y - 60.0f, 0}
        });
    });

    // Optional subtitle
    if (!p.subtitle.empty()) {
        s.layer("lt_sub", [&p](LayerBuilder& l) {
            l.text("t", {
                .content = p.subtitle,
                .style   = {.font_path = p.font_path, .size = p.sub_size,
                            .color = p.sub_color, .align = TextAlign::Left},
                .pos     = {190, p.bar_y + 28.0f, 0}
            });
        });
    }
}

} // namespace templates
} // namespace chronon3d

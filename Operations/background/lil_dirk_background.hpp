#pragma once

#include <algorithm>
#include <cmath>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>

namespace operations::background {

inline void lil_dirk_background(chronon3d::SceneBuilder& s,
                                const chronon3d::FrameContext& ctx,
                                const chronon3d::DarkGridBgParams& p = {}) {
    const chronon3d::f32 W = static_cast<chronon3d::f32>(ctx.width > 0 ? ctx.width : 1280);
    const chronon3d::f32 H = static_cast<chronon3d::f32>(ctx.height > 0 ? ctx.height : 720);

    chronon3d::Camera2_5D cam;
    cam.enabled                   = true;
    cam.position                  = {0.f, 0.f, -H};
    cam.point_of_interest         = {0.f, 0.f, 0.f};
    cam.point_of_interest_enabled = true;
    cam.zoom                      = H;
    s.camera_2_5d(cam);

    s.layer("nbg_bg", [p, W, H](chronon3d::LayerBuilder& l) {
        l.rect("r", {.size = {W, H}, .color = p.bg_color, .pos = {0.f, 0.f, 0.f}});
    });

    s.layer("nbg_lines", [p, W, H](chronon3d::LayerBuilder& l) {
        const chronon3d::f32 start_x = -W * 0.5f;
        const chronon3d::f32 start_y = -H * 0.5f;
        const chronon3d::f32 end_x = W * 0.5f;
        const chronon3d::f32 end_y = H * 0.5f;
        const chronon3d::f32 major_step = p.spacing * 4.0f;
        chronon3d::Color major_color = p.grid_color;
        major_color.a = std::min(1.0f, major_color.a * 4.0f);

        for (chronon3d::f32 x = start_x; x <= end_x; x += p.spacing) {
            l.line("vx", {.from = {x, start_y, 0.f}, .to = {x, end_y, 0.f}, .thickness = 1.5f, .color = p.grid_color});
        }
        for (chronon3d::f32 y = start_y; y <= end_y; y += p.spacing) {
            l.line("hy", {.from = {start_x, y, 0.f}, .to = {end_x, y, 0.f}, .thickness = 1.5f, .color = p.grid_color});
        }
        for (chronon3d::f32 x = start_x; x <= end_x; x += major_step) {
            l.line("Vx", {.from = {x, start_y, 0.f}, .to = {x, end_y, 0.f}, .thickness = 3.0f, .color = major_color});
        }
        for (chronon3d::f32 y = start_y; y <= end_y; y += major_step) {
            l.line("Hy", {.from = {start_x, y, 0.f}, .to = {end_x, y, 0.f}, .thickness = 3.0f, .color = major_color});
        }
    });
}

} // namespace operations::background

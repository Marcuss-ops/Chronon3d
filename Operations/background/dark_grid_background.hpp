#pragma once

#include <algorithm>
#include <cmath>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>

namespace operations::background {

inline void dark_grid_background(chronon3d::SceneBuilder& s,
                                const chronon3d::FrameContext& ctx,
                                const chronon3d::DarkGridBgParams& p = {}) {
    const chronon3d::f32 W = static_cast<chronon3d::f32>(ctx.width > 0 ? ctx.width : 1280);
    const chronon3d::f32 H = static_cast<chronon3d::f32>(ctx.height > 0 ? ctx.height : 720);

    s.layer("nbg_bg", [p, W, H](chronon3d::LayerBuilder& l) {
        l.rect("r", {.size = {W, H}, .color = p.bg_color, .pos = {W * 0.5f, H * 0.5f, 0.f}});
    });

    s.layer("nbg_lines", [p, W, H](chronon3d::LayerBuilder& l) {
        const chronon3d::f32 start_x = 0.0f;
        const chronon3d::f32 start_y = 0.0f;
        const chronon3d::f32 end_x = W;
        const chronon3d::f32 end_y = H;
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

inline chronon3d::Composition dark_grid_background_scene(
    chronon3d::i32 width = 1280,
    chronon3d::i32 height = 720,
    const chronon3d::DarkGridBgParams& p = {},
    chronon3d::Frame duration = 1
) {
    chronon3d::CompositionSpec spec{
        .name = "DarkGridBackground",
        .width = width,
        .height = height,
        .duration = duration
    };

    return chronon3d::Composition(spec, [p](const chronon3d::FrameContext& ctx) {
        chronon3d::SceneBuilder s(ctx.resource);
        dark_grid_background(s, ctx, p);
        return s.build();
    });
}

} // namespace operations::background

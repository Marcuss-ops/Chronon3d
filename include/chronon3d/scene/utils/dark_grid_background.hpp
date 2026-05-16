#pragma once

#include <algorithm>
#include <cmath>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>

namespace chronon3d::scene::utils {

inline void dark_grid_background(SceneBuilder& s,
                                const FrameContext& ctx,
                                const DarkGridBgParams& p = {}) {
    const f32 W = static_cast<f32>(ctx.width > 0 ? ctx.width : 1280);
    const f32 H = static_cast<f32>(ctx.height > 0 ? ctx.height : 720);

    s.layer("nbg_bg", [p, W, H](LayerBuilder& l) {
        l.rect("r", {.size = {W, H}, .color = p.bg_color, .pos = {W * 0.5f, H * 0.5f, 0.f}});
    });

    s.layer("nbg_lines", [p, W, H](LayerBuilder& l) {
        const f32 start_x = 0.0f;
        const f32 start_y = 0.0f;
        const f32 end_x = W;
        const f32 end_y = H;
        const f32 major_step = p.spacing * 4.0f;
        Color major_color = p.grid_color;
        major_color.a = std::min(1.0f, major_color.a * 4.0f);

        for (f32 x = start_x; x <= end_x; x += p.spacing) {
            l.line("vx", {.from = {x, start_y, 0.f}, .to = {x, end_y, 0.f}, .thickness = 1.5f, .color = p.grid_color});
        }
        for (f32 y = start_y; y <= end_y; y += p.spacing) {
            l.line("hy", {.from = {start_x, y, 0.f}, .to = {end_x, y, 0.f}, .thickness = 1.5f, .color = p.grid_color});
        }
        for (f32 x = start_x; x <= end_x; x += major_step) {
            l.line("Vx", {.from = {x, start_y, 0.f}, .to = {x, end_y, 0.f}, .thickness = 3.0f, .color = major_color});
        }
        for (f32 y = start_y; y <= end_y; y += major_step) {
            l.line("Hy", {.from = {start_x, y, 0.f}, .to = {end_x, y, 0.f}, .thickness = 3.0f, .color = major_color});
        }
    });
}

inline Composition dark_grid_background_scene(
    i32 width = 1280,
    i32 height = 720,
    const DarkGridBgParams& p = {},
    Frame duration = 1
) {
    CompositionSpec spec{
        .name = "DarkGridBackground",
        .width = width,
        .height = height,
        .duration = duration
    };

    return Composition(spec, [p](const FrameContext& ctx) {
        SceneBuilder s(ctx.resource);
        dark_grid_background(s, ctx, p);
        return s.build();
    });
}

} // namespace chronon3d::scene::utils

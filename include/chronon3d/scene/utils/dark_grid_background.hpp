#pragma once

#include <algorithm>
#include <cmath>
#include <utility>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/scene/path.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>

namespace chronon3d::scene::utils {

inline void dark_grid_background(SceneBuilder& s,
                                const FrameContext& ctx,
                                const DarkGridBgParams& p = {}) {
    const f32 W = static_cast<f32>(ctx.width > 0 ? ctx.width : 1280);
    const f32 H = static_cast<f32>(ctx.height > 0 ? ctx.height : 720);
    const f32 half_w = W * 0.5f;
    const f32 half_h = H * 0.5f;
    const f32 start_x = p.centered ? -half_w : 0.0f;
    const f32 start_y = p.centered ? -half_h : 0.0f;
    const f32 end_x = p.centered ? half_w : W;
    const f32 end_y = p.centered ? half_h : H;
    const Vec3 bg_pos = p.centered ? Vec3{0.0f, 0.0f, 0.0f}
                                   : Vec3{half_w, half_h, 0.0f};

    auto make_grid_path = [&](f32 step, f32 thickness, Color color) {
        PathParams pth;
        pth.stroke = {.width = thickness, .cap = LineCap::Butt, .join = LineJoin::Miter};
        pth.fill = Fill::solid_color(Color{0.0f, 0.0f, 0.0f, 0.0f});
        pth.color = color;

        for (f32 x = start_x; x <= end_x; x += step) {
            pth.commands.push_back(PathCommand::move_to({x, start_y}));
            pth.commands.push_back(PathCommand::line_to({x, end_y}));
        }
        for (f32 y = start_y; y <= end_y; y += step) {
            pth.commands.push_back(PathCommand::move_to({start_x, y}));
            pth.commands.push_back(PathCommand::line_to({end_x, y}));
        }

        return pth;
    };

    s.layer("nbg_bg", [p, W, H, bg_pos](LayerBuilder& l) {
        l.cache_static();
        l.rect("r", {.size = {W, H}, .color = p.bg_color, .pos = bg_pos});
    });

    s.layer("nbg_lines", [p, make_grid_path](LayerBuilder& l) {
        l.cache_static();
        const f32 major_step = p.spacing * 4.0f;
        Color major_color = p.grid_color;
        major_color.a = std::min(1.0f, major_color.a * 4.0f);

        auto minor_path = make_grid_path(p.spacing, 1.25f, p.grid_color);
        auto major_path = make_grid_path(major_step, 2.75f, major_color);
        l.path("minor_grid", std::move(minor_path));
        l.path("major_grid", std::move(major_path));
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

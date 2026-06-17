#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>

#include <string_view>

namespace chronon3d::api {

void render_builtin_background(SceneBuilder& s, const FrameContext& ctx, std::string_view id, const Color& bg_color) {
    if (id == "grid_clean") {
        auto path = scene::utils::detail::ensure_dark_grid_background_image(ctx.width, ctx.height, {
            .bg_color = bg_color, .grid_color = {1,1,1,0.9f}, .spacing = 160, .extent = 4000, .centered = true
        });
        s.screen_layer("grid_clean", [path, w=ctx.width, h=ctx.height](auto& l) {
            l.cache_static().image("img", {.path = path.string(), .size = {static_cast<f32>(w), static_cast<f32>(h)}});
        });
    }
}

} // namespace chronon3d::api

namespace chronon3d {

void register_background_presets_composition() {
    detail::add_builtin_composition("GridCleanBackground", []() {
        return composition({.name = "GridCleanBackground", .duration = 90}, [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            api::render_builtin_background(s, ctx, "grid_clean", Color::black());
            return s.build();
        });
    });
}

} // namespace chronon3d

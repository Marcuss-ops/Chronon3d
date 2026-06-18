#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/composition/register_builtin_compositions.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>

namespace chronon3d {

void register_scene_backgrounds() {
    // DarkGridBackground — procedural grid via native shape
    detail::add_builtin_composition("DarkGridBackground", []() {
        return scene::utils::dark_grid_background_scene(1920, 1080, {}, 150);
    });

    // GridCleanBackground — PNG-cached grid background
    detail::add_builtin_composition("GridCleanBackground", []() {
        return composition({.name = "GridCleanBackground", .duration = 90}, [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            auto path = scene::utils::detail::ensure_dark_grid_background_image(
                ctx.width, ctx.height,
                {.bg_color = Color::black(), .grid_color = {1, 1, 1, 0.9f}, .spacing = 160, .extent = 4000, .centered = true});
            s.screen_layer("grid_clean", [path, w = ctx.width, h = ctx.height](auto& l) {
                l.cache_static().image("img", {.path = path.string(), .size = {static_cast<f32>(w), static_cast<f32>(h)}});
            });
            return s.build();
        });
    });
}

} // namespace chronon3d

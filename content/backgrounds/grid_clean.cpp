// -----------------------------------------------------------------------
// content/backgrounds/grid_clean.cpp
//
// GridCleanBackground — opinionated, PNG-cached product-grade grid
// background.  Moved from src/scene/utils/dark_grid_background.cpp
// during the diet split so product-grade compositions are owned by
// content/ and only registered when CHRONON3D_BUILD_CONTENT=ON.
//
// The composition reuses scene::utils::detail::ensure_dark_grid_background_image
// from core (PNG-cached rasterization utility) — only the composition factory
// is opinionated product behaviour.
// -----------------------------------------------------------------------

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::backgrounds {

void register_grid_clean_background(CompositionRegistry& registry) {
    registry.add("GridCleanBackground", [](const CompositionProps&) {
        return composition({.name = "GridCleanBackground", .duration = 90},
            [](const FrameContext& ctx) {
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

} // namespace chronon3d::content::backgrounds

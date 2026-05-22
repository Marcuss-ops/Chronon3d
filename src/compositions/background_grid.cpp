#include <chronon3d/compositions/background_grid.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/frame_context.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>

namespace chronon3d::compositions {

Composition BackgroundGrid() {
    return composition(
        { .name = "BackgroundGrid", .width = 1280, .height = 720, .duration = 1 },
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx.resource);
            
            ::chronon3d::scene::utils::dark_grid_background(s, ctx, {
                .bg_color = Color::from_hex("#111111"),
                .grid_color = Color{1.0f, 1.0f, 1.0f, 0.15f},
                .spacing = 40.0f
            });

            return s.build();
        }
    );
}

} // namespace chronon3d::compositions

CHRONON_REGISTER_COMPOSITION("BackgroundGrid", chronon3d::compositions::BackgroundGrid)

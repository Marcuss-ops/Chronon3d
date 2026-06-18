#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>

namespace chronon3d {

// DarkGridBackground — procedural grid via native shape.
// GridCleanBackground (the opinionated PNG-cached product variant) was
// moved to content/backgrounds/grid_clean.cpp during PR 1.x so product-grade
// compositions are owned by content/ and gated by CHRONON3D_BUILD_CONTENT.
void register_dark_grid_background() {
    detail::add_builtin_composition("DarkGridBackground", []() {
        return scene::utils::dark_grid_background_scene(1920, 1080, {}, 150);
    });
}

} // namespace chronon3d

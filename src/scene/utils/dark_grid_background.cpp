#include <chronon3d/core/composition/composition_registration.hpp>
#include <chronon3d/core/composition/register_builtin_compositions.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>

namespace chronon3d {

void register_dark_grid_background() {
    detail::add_builtin_composition("DarkGridBackground", []() {
        return scene::utils::dark_grid_background_scene(1920, 1080, {}, 150);
    });
}

} // namespace chronon3d

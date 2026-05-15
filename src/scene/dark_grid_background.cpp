#include <chronon3d/core/composition_registration.hpp>
#include <Operations/background/dark_grid_background.hpp>

namespace chronon3d {
namespace {

Composition make_dark_grid_background() {
    return operations::background::dark_grid_background_scene(1920, 1080, {}, 150);
}

CHRONON_REGISTER_COMPOSITION("DarkGridBackground", make_dark_grid_background)

} // namespace
} // namespace chronon3d

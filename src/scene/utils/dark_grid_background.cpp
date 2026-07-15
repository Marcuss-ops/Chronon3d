#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>

namespace chronon3d {

// DarkGridBackground — procedural grid via native shape.
// GridCleanBackground (the opinionated PNG-cached product variant) was
// Underlying rasterization utility for grid backgrounds. The opinionated
// compositions are owned by content/ and gated by CHRONON3D_BUILD_CONTENT.
void register_dark_grid_background(CompositionRegistry& registry) {
    registry.add(CompositionDescriptor{.id = "DarkGridBackground", .factory = [](const CompositionProps&) {
        return scene::utils::dark_grid_background_scene(1920, 1080, {}, 150);
    }});
}

} // namespace chronon3d

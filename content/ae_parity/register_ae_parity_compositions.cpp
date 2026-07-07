// =============================================================================
// content/ae_parity/register_ae_parity_compositions.cpp
//
// Registers the 10 AE parity camera visual comparison scenes as CLI-renderable
// compositions.  Each scene is a factory from tests/visual/ae_parity/ that
// builds a Composition isolating one AE parity camera category.
// =============================================================================

#include "register_ae_parity_compositions.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>

// AE parity scene factories
#include "../../tests/visual/ae_parity/ae_parity_scenes.hpp"



namespace chronon3d::content::ae_parity {

void register_ae_parity_compositions(CompositionRegistry& registry) {
    registry.add("AE_CAM_01_static_grid",
        [](const CompositionProps&) { return test::make_ae_cam_01_static_grid(); });
    registry.add("AE_CAM_02_zoom_fov",
        [](const CompositionProps&) { return test::make_ae_cam_02_zoom_fov(); });
    registry.add("AE_CAM_03_two_node_poi",
        [](const CompositionProps&) { return test::make_ae_cam_03_two_node_poi(); });
    registry.add("AE_CAM_04_parent_null",
        [](const CompositionProps&) { return test::make_ae_cam_04_parent_null(); });
    registry.add("AE_CAM_05_orbit",
        [](const CompositionProps&) { return test::make_ae_cam_05_orbit(); });
    registry.add("AE_CAM_06_dolly_zoom",
        [](const CompositionProps&) { return test::make_ae_cam_06_dolly_zoom(); });
    registry.add("AE_CAM_07_gatefit",
        [](const CompositionProps&) { return test::make_ae_cam_07_gatefit(); });
    registry.add("AE_CAM_08_dof",
        [](const CompositionProps&) { return test::make_ae_cam_08_dof(); });
    registry.add("AE_CAM_09_motion_blur",
        [](const CompositionProps&) { return test::make_ae_cam_09_motion_blur(); });
    registry.add("AE_CAM_10_near_clip",
        [](const CompositionProps&) { return test::make_ae_cam_10_near_clip(); });

    // NOTE: The 5 cinematic compositions (ae_08/10/12/14 + motion_blur_text)
    // are registered via cli_init.hpp when CHRONON3D_BUILD_CONTENT is OFF,
    // or via the CLI's own source compilation when CONTENT is ON.
    // See apps/chronon3d_cli/CMakeLists.txt for the fallback source inclusion.
}

} // namespace chronon3d::content::ae_parity

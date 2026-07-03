// ---------------------------------------------------------------------------
// camera_change_policy.cpp — Camera change detection
// ---------------------------------------------------------------------------

#include "camera_change_policy.hpp"

namespace chronon3d::graph::detail {

bool camera_changed(
    const Camera2_5D& current,
    const Camera2_5D* prev,
    bool prev_valid
) {
    if (!current.enabled) {
        if (!prev_valid || !prev->enabled) return false;
        return true;
    }
    if (!prev_valid) return true;
    if (!prev->enabled) return true;
    return prev->position      != current.position  ||
           prev->zoom          != current.zoom      ||
           prev->fov_deg       != current.fov_deg   ||
           prev->rotation      != current.rotation  ||
           prev->optics_mode  != current.optics_mode  ||
           prev->point_of_interest_enabled != current.point_of_interest_enabled ||
           prev->point_of_interest != current.point_of_interest ||
           prev->parent_name != current.parent_name ||
           prev->target_name != current.target_name ||
           prev->hierarchy_baked != current.hierarchy_baked;
}

} // namespace chronon3d::graph::detail

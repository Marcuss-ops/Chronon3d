#include "scene_temporal_policy.hpp"

namespace chronon3d::graph::detail {

RenderSettings make_isolated_temporal_settings(const RenderSettings& settings) {
    auto isolated = settings;
    isolated.dirty.enabled = false;
    isolated.dirty.use_bitmask = false;
    isolated.dirty.use_tiles = false;
    isolated.dirty.tile_size = 0;
    isolated.diagnostics.enabled = false;
    isolated.diagnostics.plan = false;
    isolated.diagnostics.plan_output.clear();
    isolated.text_layout_debug = false;
    isolated.diagnostic_overlay_only = false;
    return isolated;
}

bool resolve_initial_clear_skip(
    const bool graph_requests_skip,
    const bool has_projected_surface) noexcept {
    return graph_requests_skip && !has_projected_surface;
}

} // namespace chronon3d::graph::detail

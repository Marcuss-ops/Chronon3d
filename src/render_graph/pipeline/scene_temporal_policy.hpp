#pragma once

#include <chronon3d/backends/software/render_settings.hpp>

namespace chronon3d::graph::detail {

/// Make the isolated temporal-sample settings used by the scene pipeline.
/// Temporal samples must not mutate dirty history or emit diagnostics into the
/// parent frame; all other render settings remain inherited from the caller.
[[nodiscard]] RenderSettings make_isolated_temporal_settings(
    const RenderSettings& settings);

/// Reuse of a full-frame seed is unsafe when a camera-projected surface can
/// expose pixels outside its current bounds. In that case the destination
/// must be cleared before compositing the next frame.
[[nodiscard]] bool resolve_initial_clear_skip(
    bool graph_requests_skip,
    bool has_projected_surface) noexcept;

} // namespace chronon3d::graph::detail

#pragma once

#include <chronon3d/backends/software/render_settings.hpp>

namespace chronon3d::graph::detail {

/// Make the isolated temporal-sample settings used by the scene pipeline.
/// Temporal samples must not mutate dirty history or emit diagnostics into the
/// parent frame; all other render settings remain inherited from the caller.
[[nodiscard]] RenderSettings make_isolated_temporal_settings(
    const RenderSettings& settings);

} // namespace chronon3d::graph::detail

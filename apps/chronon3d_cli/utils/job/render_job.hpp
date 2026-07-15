#pragma once

#include "../../commands.hpp"
#include "cli_render_utils.hpp"

#include <chronon3d/core/types/result.hpp>
#include <chronon3d/timeline/render_job.hpp>

#include <optional>

namespace chronon3d::cli {

/// Convert CLI render arguments into the canonical RenderJob value.
std::optional<RenderJob> make_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args);

/// Props-aware canonical job builder. CompositionProps flow through the
/// existing registry descriptor/factory; no second decoder or renderer config.
std::optional<RenderJob> make_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args,
                                         const CompositionProps& props);

/// Canonical immutable executor. The job carries all pinned inputs and is not
/// mutated while backend/runtime/cache state is created behind the facade.
Result<RenderJobOutput, RenderJobError> execute_render_job(const RenderJob& job);

} // namespace chronon3d::cli

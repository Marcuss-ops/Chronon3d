#pragma once

#include "../../commands.hpp"
#include "cli_render_utils.hpp"

#include <chronon3d/core/types/result.hpp>
#include <chronon3d/timeline/render_job.hpp>

#include <optional>

namespace chronon3d::cli {

/// Compatibility name only: there is no longer a second job-plan structure.
using RenderJobPlan [[deprecated("Use chronon3d::RenderJob")]] = chronon3d::RenderJob;

/// Convert CLI render arguments into the canonical RenderJob value.
std::optional<RenderJob> make_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args);

/// Props-aware canonical job builder. CompositionProps flow through the
/// existing registry descriptor/factory; no second decoder or renderer config.
std::optional<RenderJob> make_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args,
                                         const CompositionProps& props);

/// Transitional source-compatible name used by preview and older callers.
[[deprecated("Use make_render_job()")]]
std::optional<RenderJob> plan_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args);

[[deprecated("Use make_render_job()")]]
std::optional<RenderJob> plan_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args,
                                         const CompositionProps& props);

/// Canonical executor.  The job must carry a pinned registry and composition.
Result<RenderJobOutput, RenderJobError> execute_render_job(RenderJob& job);

/// One-release compatibility adapter for old two-argument call sites.
[[deprecated("Pin job.registry and call execute_render_job(job)")]]
bool execute_render_job(const CompositionRegistry& registry, RenderJob& job);

} // namespace chronon3d::cli

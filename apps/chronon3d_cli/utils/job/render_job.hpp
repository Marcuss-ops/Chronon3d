#pragma once

#include "../../commands.hpp"
#include "cli_render_utils.hpp"

#include <chronon3d/core/types/result.hpp>
#include <chronon3d/timeline/render_job.hpp>

#include <optional>

namespace chronon3d::cli {

/// Source-compatible name only: there is no second job-plan structure.
/// Remove after all internal callers use RenderJob directly.
using RenderJobPlan = chronon3d::RenderJob;

/// Convert CLI render arguments into the canonical RenderJob value.
std::optional<RenderJob> make_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args);

/// Props-aware canonical job builder. CompositionProps flow through the
/// existing registry descriptor/factory; no second decoder or renderer config.
std::optional<RenderJob> make_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args,
                                         const CompositionProps& props);

/// One-release compatibility names used by older internal callers.
std::optional<RenderJob> plan_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args);
std::optional<RenderJob> plan_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args,
                                         const CompositionProps& props);

/// Canonical executor.  The job must carry a pinned registry and composition.
Result<RenderJobOutput, RenderJobError> execute_render_job(RenderJob& job);

/// One-release compatibility adapter for old two-argument call sites.
bool execute_render_job(const CompositionRegistry& registry, RenderJob& job);

} // namespace chronon3d::cli

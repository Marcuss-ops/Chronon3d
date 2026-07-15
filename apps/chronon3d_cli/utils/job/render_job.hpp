#pragma once

#include "../../commands.hpp"
#include "cli_render_utils.hpp"

#include <chronon3d/core/types/result.hpp>
#include <chronon3d/timeline/render_job.hpp>

#include <optional>

namespace chronon3d::cli {

/// Convert CLI arguments into an unresolved RenderRequest. No Composition,
/// renderer, resolver, cache, or runtime is created at this stage.
std::optional<RenderRequest> make_render_request(
    const CompositionRegistry& registry,
    const RenderArgs& args,
    const CompositionProps& props = {});

/// Resolve logical composition input into the one canonical execution job.
Result<RenderJob, RenderJobError> resolve_render_request(
    const CompositionRegistry& registry,
    RenderRequest request);

/// Compatibility convenience for callers that plan and resolve in one call.
std::optional<RenderJob> make_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args);

std::optional<RenderJob> make_render_job(const CompositionRegistry& registry,
                                         const RenderArgs& args,
                                         const CompositionProps& props);

/// Sole immutable executor for still, sequence, video, and selected-frame jobs.
Result<RenderJobOutput, RenderJobError> execute_render_job(const RenderJob& job);

} // namespace chronon3d::cli

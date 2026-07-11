// D1 — unified RenderJob path.
// Builds chronon3d::RenderJob from CLI args, uses it for mode/frame
// detection, then delegates to the existing execution path during
// migration.  Follow-up: execute RenderJob directly via unified executor.

#include "../../commands.hpp"
#include "../../utils/job/render_job.hpp"
#include "../../utils/job/cli_render_utils.hpp"
#include <chronon3d/timeline/render_job.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d {
namespace cli {

int command_render(const CompositionRegistry& registry, const RenderArgs& args) {
    // Resolve composition
    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) {
        spdlog::error("Unknown composition: {}", args.comp_id);
        return 1;
    }

    // Parse frame range from args.frames string
    FrameRange range = parse_frames(args.frames);

    // Build unified RenderJob — canonical job descriptor (D1)
    RenderJob job;
    job.comp_id = args.comp_id;
    job.comp    = resolved.comp;
    job.output  = args.output;

    if (range.start == range.end) {
        job.mode        = RenderMode::Still;
        job.still_frame = Frame{range.start};
    } else {
        job.mode        = RenderMode::Sequence;
        job.first_frame = Frame{range.start};
        job.last_frame  = Frame{range.end};
    }

    // D1 migration bridge: delegate to existing plan+execute path.
    // The unified RenderJob serves as the canonical descriptor; the
    // old RenderJobPlan path is the execution backend until the
    // unified executor lands in a follow-up PR.
    auto plan = plan_render_job(registry, args);
    if (!plan) {
        return 1;
    }
    return execute_render_job(registry, *plan) ? 0 : 1;
}

} // namespace cli
} // namespace chronon3d

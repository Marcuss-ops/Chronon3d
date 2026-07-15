#include "../../commands.hpp"
#include "../../utils/common/render_job_error_formatter.hpp"
#include "../../utils/job/render_job.hpp"

#include <spdlog/spdlog.h>

namespace chronon3d::cli {

int command_render(const CompositionRegistry& registry, const RenderArgs& args) {
    auto job = make_render_job(registry, args);
    if (!job) {
        return 1;
    }

    auto result = execute_render_job(*job);
    if (!result) {
        if (job->mode == RenderMode::Video) {
            print_render_error(result.error(), *job);
        } else {
            // Frame-level image failures are already printed at the exact
            // renderer/output boundary with layer and frame context.
            spdlog::error("Render job failed: {}", result.error().message);
        }
        return 1;
    }

    return 0;
}

} // namespace chronon3d::cli

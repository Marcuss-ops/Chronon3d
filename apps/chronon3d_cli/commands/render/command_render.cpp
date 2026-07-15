#include "../../commands.hpp"
#include "../../utils/common/render_job_error_formatter.hpp"
#include "../../utils/job/render_job.hpp"

#include <spdlog/spdlog.h>

namespace chronon3d::cli {

int command_render(const CompositionRegistry& registry,
                   const RenderArgs& args,
                   const CompositionProps& props) {
    auto request = make_render_request(registry, args, props);
    if (!request) {
        return 1;
    }

    auto resolved = resolve_render_request(registry, std::move(*request));
    if (!resolved) {
        spdlog::error("Failed to resolve render request: {}", resolved.error().message);
        return 1;
    }

    auto result = execute_render_job(*resolved);
    if (!result) {
        if (resolved->mode == RenderMode::Video) {
            print_render_error(result.error(), *resolved);
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

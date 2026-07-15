#include "../../commands.hpp"
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
        spdlog::error("Render job failed: {}", result.error().message);
        return 1;
    }

    return 0;
}

} // namespace chronon3d::cli

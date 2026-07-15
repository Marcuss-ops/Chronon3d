#include "../../utils/job/render_job.hpp"
#include "../../utils/video/video_job_plan.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

int command_video(const CompositionRegistry& registry, const VideoArgs& args) {
    fmt::print(stderr, "Deprecated: use chronon render {} -o {}.mp4\n",
               args.comp_id, args.comp_id);

    auto job = make_video_render_job(registry, args);
    if (!job) {
        return 1;
    }

    auto result = execute_render_job(*job);
    if (!result) {
        spdlog::error("Video render job failed: {}", result.error().message);
        return 1;
    }

    return 0;
}

} // namespace chronon3d::cli

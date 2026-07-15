#include "../../utils/job/render_job.hpp"
#include "../../utils/video/video_job_plan.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <cstdio>

namespace chronon3d::cli {

int command_video(const CompositionRegistry& registry, const VideoArgs& args) {
    const std::string replacement_output = args.output.empty()
        ? args.comp_id + ".mp4"
        : args.output;
    fmt::print(stderr, "Deprecated: use chronon render {} -o {}\n",
               args.comp_id, replacement_output);

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

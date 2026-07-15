#include "../../utils/job/render_job.hpp"

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

    auto request = make_render_request(registry, args);
    if (!request) {
        return 1;
    }

    auto resolved = resolve_render_request(registry, std::move(*request));
    if (!resolved) {
        spdlog::error("Failed to resolve video request: {}", resolved.error().message);
        return 1;
    }

    auto result = execute_render_job(*resolved);
    if (!result) {
        spdlog::error("Video render job failed: {}", result.error().message);
        return 1;
    }

    return 0;
}

} // namespace chronon3d::cli

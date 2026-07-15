#include "../../commands.hpp"
#include "../../utils/job/cli_render_utils.hpp"
#include <filesystem>
#include <cstdlib>
#include <spdlog/spdlog.h>

namespace chronon3d {
namespace cli {

int command_doctor(const CompositionRegistry& registry) {
    bool ok = true;

    const std::filesystem::path camera_reference{"assets/images/camera_reference.jpg"};
    const bool camera_reference_exists = std::filesystem::exists(camera_reference);

    spdlog::info("doctor: compositions={}", registry.available().size());
    spdlog::info("doctor: camera reference {}", camera_reference_exists ? "found" : "missing");
    if (!camera_reference_exists) {
        ok = false;
    }

    {
        const bool sys_ffmpeg =
            (std::system("ffmpeg -version > /dev/null 2>&1") == 0);
        spdlog::info("doctor: ffmpeg (system PATH)   {}", sys_ffmpeg ? "found" : "NOT found");
        if (!sys_ffmpeg) ok = false;
    }

    return ok ? 0 : 1;
}

int command_verify(const CompositionRegistry& registry, const std::string& output_dir) {
    std::filesystem::create_directories(output_dir);
    int exit_code = 0;

#ifdef CHRONON3D_HAS_CLI_RENDER
    for (const auto& id : registry.available()) {
        RenderArgs args;
        args.comp_id = id;
        args.frames = "0";
        args.output = output_dir + "/" + id + ".png";
        if (command_render(registry, args) != 0) {
            exit_code = 1;
        }
    }
#else
    spdlog::warn("verify: CHRONON3D_HAS_CLI_RENDER off — per-composition render loop skipped");
    // Still mark verify incomplete so callers know the per-frame smoke wasn't run.
    exit_code = 1;
#endif

#if defined(CHRONON3D_HAS_CLI_RENDER) && defined(CHRONON3D_HAS_CLI_VIDEO_EXPORT)
    const auto available = registry.available();
    if (!available.empty()) {
        RenderArgs video_args;
        video_args.comp_id = available.front();
        video_args.frames = "0-1";
        video_args.output =
            (std::filesystem::path(output_dir) / "video_smoke_verify.mp4").string();
        if (command_render(registry, video_args) != 0) {
            exit_code = 1;
        }
    }
#endif

    return exit_code;
}

} // namespace cli
} // namespace chronon3d

#include "../commands.hpp"
#include "../utils/cli_render_utils.hpp"
#include <filesystem>
#include <cstdlib>
#include <spdlog/spdlog.h>

#ifdef CHRONON_WITH_VIDEO
#include <chronon3d/animations/camera_motion.hpp>
#include <chronon3d/backends/ffmpeg/ffmpeg_encoder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/video/video_export.hpp>
#endif

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

#ifdef CHRONON_WITH_VIDEO
    const bool video_ok = video::FfmpegEncoder::is_available();
    spdlog::info("doctor: video backend (SDK)    {}", video_ok ? "available" : "missing");
    if (!video_ok) ok = false;
#else
    {
        const bool sys_ffmpeg =
#ifdef _WIN32
            (std::system("ffmpeg -version > NUL 2>&1") == 0);
#else
            (std::system("ffmpeg -version > /dev/null 2>&1") == 0);
#endif
        spdlog::info("doctor: ffmpeg (system PATH)   {}", sys_ffmpeg ? "found" : "NOT found");
        if (!sys_ffmpeg) ok = false;
    }
#endif

    return ok ? 0 : 1;
}

int command_verify(const CompositionRegistry& registry, const std::string& output_dir) {
    std::filesystem::create_directories(output_dir);
    int exit_code = 0;

    for (const auto& id : registry.available()) {
        RenderArgs args;
        args.comp_id = id;
        args.frames = "0";
        args.output = output_dir + "/" + id + ".png";
        if (command_render(registry, args) != 0) {
            exit_code = 1;
        }
    }

#ifdef CHRONON_WITH_VIDEO
    {
        VideoCameraArgs camera_args;
        camera_args.axis = "Pan";
        camera_args.output = (std::filesystem::path(output_dir) / "camera_pan_smoke_verify.mp4").string();
        if (command_video_camera(registry, camera_args) != 0) {
            exit_code = 1;
        }
    }
#endif

    return exit_code;
}

} // namespace cli
} // namespace chronon3d

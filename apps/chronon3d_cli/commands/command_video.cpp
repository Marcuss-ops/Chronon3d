#include "../commands.hpp"
#include "../utils/ffmpeg_runner.hpp"
#include <spdlog/spdlog.h>
#include <filesystem>

namespace chronon3d {
namespace cli {

int command_video(const CompositionRegistry& registry, const VideoArgs& args) {
    if (!registry.contains(args.comp_id)) {
        spdlog::error("Unknown composition: {}", args.comp_id);
        return 1;
    }
    if (args.end <= args.start) {
        spdlog::error("--end ({}) must be greater than --start ({})", args.end, args.start);
        return 1;
    }

    namespace fs = std::filesystem;

    const fs::path frames_dir = args.frames_dir.empty()
        ? fs::path("output") / "frames" / args.comp_id
        : fs::path(args.frames_dir);

    std::error_code ec;
    fs::create_directories(frames_dir, ec);
    if (ec) {
        spdlog::error("Cannot create frames directory {}: {}", frames_dir.string(), ec.message());
        return 1;
    }

    const std::string frame_pattern = (frames_dir / "frame_%04d.png").string();

    RenderArgs render_args;
    render_args.comp_id              = args.comp_id;
    render_args.start_old            = args.start;
    render_args.end_old              = args.end;
    render_args.output               = (frames_dir / "frame_####.png").string();
    render_args.use_modular_graph    = args.use_modular_graph;
    render_args.motion_blur          = args.motion_blur;
    render_args.motion_blur_samples  = args.motion_blur_samples;
    render_args.shutter_angle        = args.shutter_angle;

    spdlog::info("Rendering {} frames {} → {} ...", args.comp_id, args.start, args.end);
    const int render_result = command_render(registry, render_args);
    if (render_result != 0) {
        spdlog::error("Frame rendering failed");
        return render_result;
    }

    if (!ffmpeg_available()) {
        spdlog::error("ffmpeg not found in PATH — install ffmpeg or add it to PATH");
        return 1;
    }

    const fs::path out_path(args.output);
    fs::create_directories(out_path.parent_path(), ec);

    spdlog::info("Encoding {} → {}", frame_pattern, args.output);
    const int ff_result = run_ffmpeg_encode(frame_pattern, args.output, args.fps, args.crf, args.preset);
    if (ff_result != 0) {
        spdlog::error("ffmpeg exited with code {}", ff_result);
        return ff_result;
    }

    if (!fs::exists(out_path) || fs::file_size(out_path) == 0) {
        spdlog::error("Output video missing or empty: {}", args.output);
        return 1;
    }

    if (!args.keep_frames) {
        fs::remove_all(frames_dir, ec);
        if (ec) spdlog::warn("Could not remove frames dir {}: {}", frames_dir.string(), ec.message());
    }

    spdlog::info("Video saved to {}", args.output);
    return 0;
}

} // namespace cli
} // namespace chronon3d

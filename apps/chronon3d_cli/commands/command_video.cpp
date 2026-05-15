#include "../commands.hpp"
#include "../utils/cli_render_utils.hpp"
#include <spdlog/spdlog.h>

#ifdef CHRONON_WITH_VIDEO
#include <chronon3d/video/ffmpeg_encoder.hpp>
#include <chronon3d/renderer/software/software_renderer.hpp>
#include <filesystem>
#include <chrono>

namespace chronon3d {
namespace cli {

int command_video(const CompositionRegistry& registry, const VideoArgs& args) {
    if (args.end <= args.start) {
        spdlog::error("--end ({}) must be greater than --start ({})", args.end, args.start);
        return 1;
    }

    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) return 1;

    RenderSettings settings;
    settings.diagnostic = false;
    settings.use_modular_graph = args.use_modular_graph;
    settings.motion_blur.enabled = resolved.from_specscene ? false : args.motion_blur;
    settings.motion_blur.samples = args.motion_blur_samples;
    settings.motion_blur.shutter_angle = args.shutter_angle;
    settings.ssaa_factor = args.ssaa;

    auto renderer = create_renderer(registry, settings);

    video::VideoEncodeOptions options;
    options.fps = args.fps;
    options.crf = args.crf;
    options.preset = args.preset;

    video::FfmpegEncoder encoder;

    const std::filesystem::path out_path(args.output);
    if (out_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(out_path.parent_path(), ec);
    }

    spdlog::info("Encoding video {}x{} @ {}fps to {} ...", resolved.comp->width(), resolved.comp->height(), options.fps, args.output);

    if (!encoder.open(args.output, options, resolved.comp->width(), resolved.comp->height())) {
        spdlog::error("Failed to open video encoder for {}", args.output);
        return 1;
    }

    auto t0 = std::chrono::steady_clock::now();

    for (int64_t f = args.start; f < args.end; ++f) {
        auto fb = renderer->render_frame(*resolved.comp, static_cast<Frame>(f));
        if (!fb) {
            spdlog::error("Failed to render frame {}", f);
            return 1;
        }
        if (!encoder.push_frame(*fb)) {
            spdlog::error("Failed to push frame {} to encoder", f);
            return 1;
        }

        if ((f - args.start + 1) % 10 == 0 || (f + 1) == args.end) {
            spdlog::info("  Progress: {}/{} frames", f - args.start + 1, args.end - args.start);
        }
    }

    encoder.close();

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    spdlog::info("Video saved to {} (total time: {}ms)", args.output, ms);
    return 0;
}

} // namespace cli
} // namespace chronon3d

#else

namespace chronon3d::cli {
int command_video(const CompositionRegistry&, const VideoArgs&) {
    spdlog::error("Chronon3D was built without video support (CHRONON_WITH_VIDEO).");
    return 1;
}
}

#endif

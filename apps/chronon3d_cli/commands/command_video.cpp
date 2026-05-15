#include "../commands.hpp"
#include "../utils/cli_render_utils.hpp"
#include <spdlog/spdlog.h>

#ifdef CHRONON_WITH_VIDEO
#include <chronon3d/backends/ffmpeg/video_export.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

namespace chronon3d {
namespace cli {

int command_video(const CompositionRegistry& registry, const VideoArgs& args) {
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

    video::VideoExportOptions options;
    options.start = args.start;
    options.end = args.end;
    options.encode.fps = args.fps;
    options.encode.crf = args.crf;
    options.encode.preset = args.preset;

    return video::render_to_mp4(*renderer, *resolved.comp, args.output, options) ? 0 : 1;
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

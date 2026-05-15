#include <chronon3d/backends/ffmpeg/video_export.hpp>

#include <chrono>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace chronon3d::video {

bool render_to_mp4(SoftwareRenderer& renderer,
                   const Composition& comp,
                   const std::string& output_path,
                   const VideoExportOptions& options) {
    if (options.end <= options.start) {
        spdlog::error("Invalid export range: end ({}) must be greater than start ({})",
                      options.end, options.start);
        return false;
    }

    FfmpegEncoder encoder;
    const std::filesystem::path out_path(output_path);
    if (out_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(out_path.parent_path(), ec);
    }

    spdlog::info("Encoding video {}x{} @ {}fps to {} ...",
                 comp.width(), comp.height(), options.encode.fps, output_path);

    if (!encoder.open(output_path, options.encode, comp.width(), comp.height())) {
        spdlog::error("Failed to open video encoder for {}", output_path);
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();
    const Frame total = options.end - options.start;

    for (Frame f = options.start; f < options.end; ++f) {
        auto fb = renderer.render_frame(comp, f);
        if (!fb) {
            spdlog::error("Failed to render frame {}", f);
            return false;
        }
        if (!encoder.push_frame(*fb)) {
            spdlog::error("Failed to push frame {} to encoder", f);
            return false;
        }

        if (((f - options.start + 1) % 10) == 0 || (f + 1) == options.end) {
            spdlog::info("  Progress: {}/{} frames", f - options.start + 1, total);
        }
    }

    encoder.close();

    const auto t1 = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    spdlog::info("Video saved to {} (total time: {}ms)", output_path, ms);
    return true;
}

} // namespace chronon3d::video

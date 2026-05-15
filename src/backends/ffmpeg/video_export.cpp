#include <chronon3d/backends/video/video_export.hpp>
#include <chronon3d/backends/ffmpeg/ffmpeg_encoder.hpp>
#include <chronon3d/core/render_telemetry.hpp>

#include <chrono>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace chronon3d::video {

bool render_to_video(SoftwareRenderer& renderer,
                     const Composition& comp,
                     const std::string& output_path,
                     const VideoExportOptions& options,
                     IEncoder& encoder) {
    if (options.end <= options.start) {
        spdlog::error("Invalid export range: end ({}) must be greater than start ({})",
                      options.end, options.start);
        return false;
    }

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
        const auto layer_count = static_cast<int>(comp.evaluate(f).layers().size());
        const auto hits_before = renderer.node_cache().stats().hits;
        const auto t_render0 = std::chrono::steady_clock::now();
        auto fb = renderer.render_frame(comp, f);
        const auto t_render1 = std::chrono::steady_clock::now();
        if (!fb) {
            spdlog::error("Failed to render frame {}", f);
            return false;
        }
        const auto hits_after = renderer.node_cache().stats().hits;
        const auto t_encode0 = std::chrono::steady_clock::now();
        if (!encoder.push_frame(*fb)) {
            spdlog::error("Failed to push frame {} to encoder", f);
            return false;
        }
        const auto t_encode1 = std::chrono::steady_clock::now();

        telemetry::record_render_telemetry({
            .event = "video_frame",
            .frame = f,
            .width = comp.width(),
            .height = comp.height(),
            .total_ms = std::chrono::duration<double, std::milli>(t_encode1 - t_render0).count(),
            .setup_ms = 0.0,
            .composite_ms = 0.0,
            .blur_ms = 0.0,
            .encode_ms = std::chrono::duration<double, std::milli>(t_encode1 - t_encode0).count(),
            .cache_hit = hits_after > hits_before ? 1 : 0,
            .layer_count = layer_count,
        });

        if (((f - options.start + 1) % 10) == 0 || (f + 1) == options.end) {
            spdlog::info("  Progress: {}/{} frames", f - options.start + 1, total);
        }
    }

    encoder.close();

    const auto t1 = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    telemetry::record_render_telemetry({
        .event = "video_export",
        .frame = options.start,
        .width = comp.width(),
        .height = comp.height(),
        .total_ms = static_cast<double>(ms),
        .layer_count = static_cast<int>(comp.evaluate(options.start).layers().size()),
    });
    spdlog::info("Video saved to {} (total time: {}ms)", output_path, ms);
    return true;
}

} // namespace chronon3d::video

#include "render_job_detail.hpp"

#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/render_telemetry.hpp>
#include <chronon3d/core/profiling.hpp>
#include <chronon3d/core/trace.hpp>

#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>

namespace chronon3d::cli {

bool write_render_frame(const Composition& comp,
                        SoftwareRenderer& renderer,
                        Frame frame,
                        const FrameRange& range,
                        const std::string& output_pattern,
                        bool& ok,
                        std::vector<telemetry::FrameTelemetryRecord>& telemetry_frames,
                        double& total_render_ms,
                        double& total_encode_ms,
                        int& frames_written) {
    const auto hits_before = renderer.node_cache().stats().hits;
    const auto t0 = std::chrono::steady_clock::now();
    auto fb = renderer.render_frame(comp, frame);
    const auto t1 = std::chrono::steady_clock::now();
    const auto hits_after = renderer.node_cache().stats().hits;
    const double dirty_ratio = renderer.last_dirty_area_ratio();
    const auto layer_count = renderer.last_layer_count();

    if (!fb) {
        spdlog::error("Failed to render frame {}", frame);
        ok = false;
        return false;
    }

    const bool is_range = (range.start != range.end);
    const std::string path = format_path(output_pattern, frame, is_range);
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    const auto t_encode0 = std::chrono::steady_clock::now();

    ImageWriteOptions write_options;
    write_options.format = image_format_from_path(path);

    if (write_options.format == ImageFormat::Unknown) {
        spdlog::error("Unsupported image output format for path: {}", path);
        ok = false;
        return false;
    }

    // Set trace context so CHRONON_ZONE zones inside save_image work.
    profiling::g_current_trace = renderer.trace();
    profiling::g_current_frame = static_cast<int32_t>(frame);
    {
        CHRONON_ZONE_C("write_frame_to_disk", trace_category::kOutput);
        if (!save_image(*fb, path, write_options)) {
            spdlog::error("Failed to save frame {} to {} as {}",
                          frame,
                          path,
                          image_format_name(write_options.format));
            ok = false;
            return false;
        }
    }
    profiling::g_current_trace = nullptr;

    const auto t_encode1 = std::chrono::steady_clock::now();

    const double render_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    const double encode_ms = std::chrono::duration<double, std::milli>(t_encode1 - t_encode0).count();
    total_render_ms += render_ms;
    total_encode_ms += encode_ms;
    frames_written++;

    const bool hit = (hits_after > hits_before);

    // Record Legacy Telemetry CSV
    telemetry::record_render_telemetry({
        .event = "image_render",
        .frame = frame,
        .width = comp.width(),
        .height = comp.height(),
        .total_ms = std::chrono::duration<double, std::milli>(t_encode1 - t0).count(),
        .setup_ms = render_ms,
        .encode_ms = encode_ms,
        .cache_hit = hit ? 1 : 0,
        .layer_count = layer_count,
    });

    // Record Unified Telemetry Frame Record
    telemetry_frames.push_back({
        .frame_number = static_cast<int>(frame),
        .duration_ms = render_ms + encode_ms,
        .cache_hit = hit,
        .dirty_area_ratio = dirty_ratio
    });

    spdlog::info("Frame {} saved to {}", frame, path);
    return true;
}

} // namespace chronon3d::cli

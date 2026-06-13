#include "render_job_detail.hpp"

#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include <spdlog/spdlog.h>

#include <filesystem>

namespace chronon3d::cli {

bool write_render_frame(const Composition& comp,
                        SoftwareRenderer& renderer,
                        Frame frame,
                        const FrameRange& range,
                        const std::string& output_pattern,
                        bool& ok,
                        std::vector<chronon3d::telemetry::FrameTelemetryRecord>& telemetry_frames,
                        double& total_render_ms,
                        double& total_encode_ms,
                        int& frames_written) {
    const auto hits_before = renderer.node_cache().stats().hits;
    const auto t0 = profiling::now();
    auto fb = renderer.render_frame(comp, frame);
    const auto t1 = profiling::now();
    const auto hits_after = renderer.node_cache().stats().hits;
    const double dirty_ratio = renderer.last_dirty_area_ratio();

    if (!fb) {
        spdlog::error("Failed to render frame {}", frame);
        ok = false;
        return false;
    }

    const bool cache_hit = (hits_after > hits_before);
    const double render_ms = profiling::duration_ms(t0, t1);
    total_render_ms += render_ms;

    const int prog_cache_cap = static_cast<int>(
        renderer.counters()
            ? renderer.counters()->program_cache_capacity.load(std::memory_order_relaxed)
            : 0);

    const double encode_ms = write_frame_to_disk(
        fb, frame, range, output_pattern, cache_hit, dirty_ratio,
        render_ms, prog_cache_cap,
        ok, telemetry_frames, total_encode_ms, frames_written);

    return encode_ms >= 0.0;
}

double write_frame_to_disk(std::shared_ptr<Framebuffer> fb,
                           Frame frame,
                           const FrameRange& range,
                           const std::string& output_pattern,
                           bool cache_hit,
                           double dirty_ratio,
                           double render_ms,
                           int program_cache_capacity,
                           bool& ok,
                           std::vector<chronon3d::telemetry::FrameTelemetryRecord>& telemetry_frames,
                           double& total_encode_ms,
                           int& frames_written) {
    if (!fb) {
        spdlog::error("write_frame_to_disk: null framebuffer for frame {}", frame);
        ok = false;
        return -1.0;
    }

    const bool is_range = (range.start != range.end);
    const std::string path = format_path(output_pattern, frame, is_range);
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    const auto t_encode0 = profiling::now();

    ImageWriteOptions write_options;
    write_options.format = image_format_from_path(path);

    if (write_options.format == ImageFormat::Unknown) {
        spdlog::error("Unsupported image output format for path: {}", path);
        ok = false;
        return -1.0;
    }

    {
        CHRONON_ZONE_C("write_frame_to_disk", trace_category::kOutput);
        if (!save_image(*fb, path, write_options)) {
            spdlog::error("Failed to save frame {} to {} as {}",
                          frame,
                          path,
                          image_format_name(write_options.format));
            ok = false;
            return -1.0;
        }
    }

    const double encode_ms = profiling::elapsed_ms(t_encode0);
    total_encode_ms += encode_ms;
    frames_written++;

        telemetry_frames.emplace_back();
        auto& rec = telemetry_frames.back();
        rec.frame_number = static_cast<int>(frame);
        rec.duration_ms = render_ms + encode_ms;
        rec.cache_hit = cache_hit;
        rec.dirty_area_ratio = dirty_ratio;
        rec.graph_eval_ms = render_ms;
        rec.encoder_ms = encode_ms;
        rec.program_cache_capacity = program_cache_capacity;

    spdlog::info("Frame {} saved to {}", frame, path);
    return encode_ms;
}

} // namespace chronon3d::cli

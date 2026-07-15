#include "render_job_detail.hpp"

#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include "../common/render_error_formatter.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <filesystem>
#include <system_error>

// TICKET-RENDER-PIPELINE-INTEGRITY: pre-write framebuffer sanity scan shared
// by production and the CLI regression tests.
#include "render_job_write_frame_sanity.hpp"

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
    auto fb = renderer.render(comp, frame);
    const auto t1 = profiling::now();
    const auto hits_after = renderer.node_cache().stats().hits;
    const double dirty_ratio = renderer.last_dirty_area_ratio();

    if (!fb) {
        const auto structured = renderer.session().last_frame_error();
        if (structured) {
            print_render_error(*structured, comp.name(), frame);
        } else {
            print_render_error(
                graph::NodeExecutionError{
                    graph::RenderBackendErrorCode::ExecutionFailure,
                    "render",
                    "renderer returned a null framebuffer without a structured node error"
                },
                comp.name(),
                frame);
        }
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
        fb, frame, range, output_pattern, comp.name(), cache_hit, dirty_ratio,
        render_ms, prog_cache_cap, ok, telemetry_frames, total_encode_ms,
        frames_written);

    return encode_ms >= 0.0;
}

double write_frame_to_disk(std::shared_ptr<Framebuffer> fb,
                           Frame frame,
                           const FrameRange& range,
                           const std::string& output_pattern,
                           const std::string& composition_id,
                           bool cache_hit,
                           double dirty_ratio,
                           double render_ms,
                           int program_cache_capacity,
                           bool& ok,
                           std::vector<chronon3d::telemetry::FrameTelemetryRecord>& telemetry_frames,
                           double& total_encode_ms,
                           int& frames_written) {
    if (!fb) {
        print_render_error(
            graph::NodeExecutionError{
                graph::RenderBackendErrorCode::ExecutionFailure,
                "render",
                "write_frame_to_disk received a null framebuffer"
            },
            composition_id,
            frame);
        ok = false;
        return -1.0;
    }

    const bool is_range = (range.start != range.end);
    const std::string path = format_path(output_pattern, frame.as_i64(), is_range);
    const std::filesystem::path output_path(path);
    if (output_path.has_parent_path()) {
        std::error_code directory_error;
        std::filesystem::create_directories(output_path.parent_path(), directory_error);
        if (directory_error) {
            print_render_error(
                graph::NodeExecutionError{
                    graph::RenderBackendErrorCode::ExecutionFailure,
                    "render",
                    "failed to open output path '" + path + "': " +
                        directory_error.message()
                },
                composition_id,
                frame);
            ok = false;
            return -1.0;
        }
    }

    const auto t_encode0 = profiling::now();

    ImageWriteOptions write_options;
    write_options.format = image_format_from_path(path);

    if (write_options.format == ImageFormat::Unknown) {
        print_render_error(
            graph::NodeExecutionError{
                graph::RenderBackendErrorCode::InvalidInput,
                "render",
                "unsupported image output format for output '" + path + "'"
            },
            composition_id,
            frame);
        ok = false;
        return -1.0;
    }

    {
        const auto sanity = scan_framebuffer_sanity(*fb);
        if (!sanity.ok) {
            print_render_error(
                graph::NodeExecutionError{
                    graph::RenderBackendErrorCode::ExecutionFailure,
                    "render",
                    "framebuffer sanity validation failed before writing output '" +
                        path + "'"
                },
                composition_id,
                frame);
            ok = false;
            return -1.0;
        }
    }

    {
        CHRONON_ZONE_C("write_frame_to_disk", trace_category::kOutput);
        if (!save_image(*fb, path, write_options)) {
            print_render_error(
                graph::NodeExecutionError{
                    graph::RenderBackendErrorCode::ExecutionFailure,
                    "render",
                    "failed to save frame to output '" + path + "' as " +
                        std::string(image_format_name(write_options.format))
                },
                composition_id,
                frame);
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

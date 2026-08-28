#include "render_job_detail.hpp"

#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include "../common/render_error_formatter.hpp"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <system_error>

// TICKET-RENDER-PIPELINE-INTEGRITY: pre-write framebuffer sanity scan shared
// by production and the CLI regression tests.
#include "render_job_write_frame_sanity.hpp"

namespace chronon3d::cli {

namespace {

std::string text_layout_json_path(const std::string& pattern, Frame frame) {
    const std::string suffix = std::to_string(frame.integral());
    std::string path = pattern;
    const auto marker = path.find("####");
    if (marker != std::string::npos) {
        path.replace(marker, 4, suffix);
        return path;
    }
    const std::filesystem::path input(path);
    const auto stem = input.stem().string();
    const auto ext = input.extension().string();
    const auto parent = input.parent_path();
    const auto filename = stem + "_" + suffix + ext;
    return (parent / filename).string();
}

bool write_text_layout_json(SoftwareRenderer& renderer,
                            const Framebuffer& framebuffer,
                            Frame frame) {
    const auto& settings = renderer.render_settings();
    if (settings.text_layout_debug_json_path.empty()) return true;

    nlohmann::json root{
        {"schema_version", 1},
        {"frame", frame.integral()},
        {"width", framebuffer.width()},
        {"height", framebuffer.height()},
        {"text_runs", nlohmann::json::array()}
    };
    auto& runs = root["text_runs"];
    for (const auto& snapshot : renderer.text_audit_snapshots()) {
        const auto& b = snapshot.predicted_bbox;
        const auto& c = snapshot.clip_rect;
        nlohmann::json matrix = nlohmann::json::array();
        for (int col = 0; col < 4; ++col) {
            nlohmann::json row = nlohmann::json::array();
            for (int row_index = 0; row_index < 4; ++row_index) {
                row.push_back(snapshot.world_matrix[col][row_index]);
            }
            matrix.push_back(std::move(row));
        }
        nlohmann::json run = nlohmann::json::object();
        run["name"] = snapshot.name;
        run["predicted_bbox"] = {
            {"x0", b.origin.x}, {"y0", b.origin.y},
            {"width", b.size.x}, {"height", b.size.y}
        };
        run["clip_rect"] = {
            {"x0", c.origin.x}, {"y0", c.origin.y},
            {"width", c.size.x}, {"height", c.size.y}
        };
        run["world_matrix"] = std::move(matrix);
        runs.push_back(std::move(run));
    }

    const std::string path = text_layout_json_path(
        settings.text_layout_debug_json_path, frame);
    const std::filesystem::path output(path);
    if (output.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(output.parent_path(), ec);
        if (ec) {
            spdlog::error("Cannot create text layout JSON directory '{}': {}",
                          output.parent_path().string(), ec.message());
            return false;
        }
    }
    std::ofstream file(path);
    if (!file) {
        spdlog::error("Cannot write text layout JSON: {}", path);
        return false;
    }
    file << root.dump(2) << '\n';
    spdlog::info("Text layout JSON written to {} ({} TextRun snapshots)",
                 path, runs.size());
    return true;
}

} // namespace

bool write_render_frame(const CompiledComposition& compiled,
                        SoftwareRenderer& renderer,
                        Frame frame,
                        const FrameRange& range,
                        const std::string& output_pattern,
                        bool& ok,
                        std::vector<chronon3d::telemetry::FrameTelemetry>& telemetry_frames,
                        double& total_render_ms,
                        double& total_encode_ms,
                        int& frames_written) {
    const auto hits_before = renderer.node_cache().stats().hits;
    const auto t0 = profiling::now();
    auto fb = renderer.render_compiled(compiled, frame);
    const auto t1 = profiling::now();
    const auto hits_after = renderer.node_cache().stats().hits;
    const double dirty_ratio = renderer.last_dirty_area_ratio();

    if (!fb) {
        const auto structured = renderer.session().last_frame_error();
        if (structured) {
            print_render_error(*structured,
                               compiled.composition->name(), frame);
        } else {
            print_render_error(
                graph::NodeExecutionError{
                    graph::RenderBackendErrorCode::ExecutionFailure,
                    "render",
                    "renderer returned a null framebuffer without a structured node error"
                },
                compiled.composition->name(),
                frame);
        }
        ok = false;
        return false;
    }

    const bool cache_hit = (hits_after > hits_before);
    const double render_ms = profiling::duration_ms(t0, t1);
    total_render_ms += render_ms;

    if (!write_text_layout_json(renderer, *fb, frame)) {
        ok = false;
        return false;
    }

    const int prog_cache_cap = static_cast<int>(
        renderer.counters()
            ? renderer.counters()->program_cache_capacity.load(std::memory_order_relaxed)
            : 0);

    const double encode_ms = write_frame_to_disk(
        fb, frame, range, output_pattern,
        compiled.composition->name(), cache_hit, dirty_ratio,
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
                           std::vector<chronon3d::telemetry::FrameTelemetry>& telemetry_frames,
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
        CHRONON_TRACE_SCOPE("chronon.io", "WriteFrameToDisk");
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

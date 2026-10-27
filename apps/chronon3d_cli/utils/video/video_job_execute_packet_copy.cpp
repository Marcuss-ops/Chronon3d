#include "video_job_execute_dispatch_detail.hpp"
#include "gop_smart_copy.hpp"
#include "../../commands/video/common/pipe_export_helpers.hpp"
#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <optional>

namespace chronon3d::cli::detail {
std::optional<int> execute_packet_copy_lane(
    const FfmpegExportOptions& render_opts, double start_sec, double end_sec) {
    const auto& opts = render_opts;
    if (render_opts.resolved_execution_plan &&
        render_opts.resolved_execution_plan->decode ==
            media::DecodePath::PacketCopy &&
        render_opts.resolved_execution_plan->encode ==
            media::EncodePath::PacketCopy &&
        render_opts.resolved_execution_valid) {
        spdlog::info("[bitstream-copy] path selected (reason: {}); bypassing NVDEC/CUDA/NVENC entirely",
                     render_opts.resolved_execution_reason);
        const auto copy_t0 = profiling::now();
        if (const auto parent = std::filesystem::path(opts.output.output).parent_path();
            !parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                spdlog::error("[bitstream-copy] failed to create output directory '{}': {}",
                              parent.string(), ec.message());
                return 1;
            }
        }

        const auto final_path = std::filesystem::path(opts.output.output);
        // AUDIT §5 — the same staging path rule as the direct-mux lane.
        const auto partial_path = make_partial_output_path(final_path);

        auto copy_result = copy_gop_source(
            opts.gop_source, partial_path.string(), start_sec, end_sec);
        const double copy_ms = profiling::duration_ms(copy_t0, profiling::now());
        if (!copy_result) {
            spdlog::error("[bitstream-copy] copy_gop_source failed for source '{}' → '{}'",
                          opts.gop_source, partial_path.string());
            std::error_code rm_ec;
            std::filesystem::remove(partial_path, rm_ec);
            return 1;
        }
        std::error_code rename_ec;
        std::filesystem::rename(partial_path, final_path, rename_ec);
        if (rename_ec) {
            spdlog::error("[bitstream-copy] failed to rename '{}' → '{}': {}",
                          partial_path.string(), final_path.string(), rename_ec.message());
            return 1;
        }
        spdlog::info("[bitstream-copy] completed: {} video packets, {} audio packets in {:.1f}ms → {}",
                     copy_result->video_packets, copy_result->audio_packets,
                     copy_ms, opts.output.output);
        return 0;
    }
    return std::nullopt;

}
} // namespace chronon3d::cli::detail

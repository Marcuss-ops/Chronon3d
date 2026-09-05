// ═══════════════════════════════════════════════════════════════════════════
// pipe_timing_sidecar.cpp — frame-timing sidecar orchestrator.
//
// write_frame_timing_sidecar() was a single ~700-line function; it is now
// phased across sibling TUs (pure code move, no schema change):
//   - pipe_timing_sidecar_frames.cpp       per-frame sections (phase 1)
//   - pipe_timing_sidecar_summary.cpp      summary + job sections (phase 2)
//   - pipe_timing_sidecar_diagnostics.cpp  cache/memory/wall/etc. (phase 3)
// Shared state is threaded via pipe_timing_sidecar_detail.hpp.
// ═══════════════════════════════════════════════════════════════════════════

#include "pipe_timing_sidecar_detail.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>

namespace chronon3d::cli {
using pipe_timing::JobTimings;

void write_frame_timing_sidecar(
    const std::string& video_path,
    const std::vector<chronon3d::telemetry::FrameTelemetry>& render_frames,
    const std::vector<chronon3d::telemetry::FrameTelemetry>& encoder_frames,
    double wall_time_ms,
    double render_ms,
    double encode_ms,
    const JobTimings& timings,
    bool is_native)
{
    if (render_frames.empty()) return;

    namespace detail = chronon3d::cli::pipe_timing_detail;

    detail::SidecarContext ctx;
    ctx.frames = render_frames;
    ctx.enc = encoder_frames;
    ctx.video_path = video_path;
    ctx.wall_time_ms = wall_time_ms;
    ctx.render_ms = render_ms;
    ctx.encode_ms = encode_ms;
    ctx.is_native = is_native;

    nlohmann::json out;
    const auto durations = detail::build_frame_times_section(out, ctx);
    detail::build_summary_and_job_sections(out, ctx, durations, timings);
    detail::build_diagnostics_sections(out, timings, ctx.frames.size());

    const auto sidecar = std::filesystem::path(video_path).string() + ".timing.json";
    std::ofstream file(sidecar);
    if (!file) {
        spdlog::warn("[video] Could not write frame timing sidecar: {}", sidecar);
        return;
    }
    file << out.dump(2) << '\n';
    spdlog::info("[video] Wrote exact per-frame timing sidecar: {}", sidecar);
}

} // namespace chronon3d::cli

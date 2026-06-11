#include "../common/pipe_export_session.hpp"
#include "../common/pipe_export_helpers.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::cli {

PipeExportResult render_and_encode_ffmpeg_pipe(
    const CompositionRegistry& registry,
    const Composition& comp,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts)
{
    const auto wall_t0 = profiling::now();

    // Phase 1 — Setup
    auto session = setup_pipe_export_session(registry, comp, settings, opts, start, end);
    if (!session || !session->encoder || !session->renderer) {
        return PipeExportResult{};
    }

    if (opts.chunks != 1) {
        spdlog::warn("[video] --chunks is ignored with --ffmpeg-mode pipe in V1");
    }

    // Phase 2-4 — Warmup
    RenderSettings render_opts = settings;
    render_opts.use_modular_graph = true;
    warmup_pipe_renderer(*session->renderer, comp, opts);
    warmup_pipe_pool(*session);
    session->sys_metrics.sample_cpu_start();

    // Phase 5 — Render loop + writer join
    auto loop_output = run_pipe_export_loop(
        *session, registry, comp, render_opts, start, end, opts);

    // Phase 6 — Encoder close
    auto close_result = close_pipe_encoder(*session);

    const auto wall_t1 = profiling::now();
    const double wall_time_ms = profiling::duration_ms(wall_t0, wall_t1);
    const double encode_ms = profiling::duration_ms(loop_output.render_end, wall_t1);

    spdlog::info("[video] FFmpeg queue wait duration: {:.2f} ms", loop_output.loop_result.queue_wait_ms);

    // Phase 7 — Telemetry
    record_pipe_telemetry(composition_id, *session, loop_output.loop_result,
                          close_result, loop_output.telemetry_frames,
                          wall_time_ms, loop_output.render_ms, encode_ms);

    // Phase 8 — Result
    return make_pipe_export_result(*session, loop_output.loop_result, close_result,
                                   loop_output.render_ms, encode_ms, wall_time_ms);
}

} // namespace chronon3d::cli

#include "../common/pipe_export_session.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/telemetry/telemetry_bundle.hpp>

#include <chrono>

namespace chronon3d::cli {

RenderLoopOutput run_pipe_export_loop(
    PipeExportSession& session,
    const CompositionRegistry& registry,
    const Composition& comp,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts)
{
    cache::NodeCache node_cache;
    media::MediaFrameProvider* video_decoder = nullptr;

    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;
    telemetry_frames.reserve(session.total_frames > 0
        ? static_cast<size_t>(session.total_frames) : 0);

    const auto render_t0 = std::chrono::steady_clock::now();

    RenderLoopContext loop_ctx{
        .backend = *session.renderer,
        .node_cache = node_cache,
        .settings = settings,
        .registry = registry,
        .video_decoder = video_decoder,
        .comp = comp,
        .start = start,
        .end = end,
        .opts = opts,
        .sw_renderer = session.sw_renderer,
        .queue = session.queue,
        .writer_failed = session.writer_failed,
        .triple_arena = *session.triple_arena,
        .counters = session.renderer->counters(),
        .telemetry_frames = telemetry_frames,
    };
    auto loop_result = run_render_loop(loop_ctx);

    const auto render_t1 = std::chrono::steady_clock::now();

    // Signal writer done and join
    session.writer_done.store(true);
    if (session.writer_thread.joinable()) {
        session.writer_thread.join();
    }

    if (session.writer_failed.load()) {
        loop_result.status.success = false;
        loop_result.status.writer_error = true;
    }

    RenderLoopOutput output;
    output.loop_result = std::move(loop_result);
    output.telemetry_frames = std::move(telemetry_frames);
    output.render_ms = std::chrono::duration<double, std::milli>(render_t1 - render_t0).count();
    output.render_end = render_t1;
    return output;
}

} // namespace chronon3d::cli

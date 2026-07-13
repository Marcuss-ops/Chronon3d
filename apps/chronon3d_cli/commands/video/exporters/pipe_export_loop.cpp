#include "../common/pipe_export_session.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/telemetry/telemetry_bundle.hpp>

#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

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
    // Reuse the renderer/runtime's canonical NodeCache instead of creating a
    // second local cache.  This keeps still and video renders consistent and
    // avoids split statistics / capacity / clear behaviour.
    cache::NodeCache& node_cache = session.renderer->node_cache();
    media::MediaFrameProvider* video_decoder = nullptr;

    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;
    telemetry_frames.reserve(session.total_frames > 0
        ? static_cast<size_t>(session.total_frames) : 0);

    const auto render_t0 = profiling::now();

    RenderLoopContext loop_ctx{
        // 06 R3b boundary refactor: `SoftwareRenderer` no longer derives
        // from `graph::RenderBackend` — the backend is reachable via the
        // `->backend()` accessor (a domain-aware forwarder into the
        // runtime-owned backend slot, NOT an implicit IS-A upcast).
        .backend = session.renderer->backend(),
        .node_cache = node_cache,
        .settings = settings,
        .registry = registry,
        .video_decoder = video_decoder,
        .comp = comp,
        .start = start,
        .end = end,
        .opts = opts,
        .sw_renderer = session.renderer.get(),
        .queue = session.queue,
        .writer_failed = session.writer_failed,
        .triple_arena = *session.triple_arena,
        .counters = session.renderer->counters(),
        .telemetry_frames = telemetry_frames,
    };
    auto loop_result = run_render_loop(loop_ctx);

    const auto render_t1 = profiling::now();

    // Close the queue to unblock the writer, then join.
    session.queue.close();
    if (session.writer_thread.joinable()) {
        session.writer_thread.join();
    }

    if (session.writer_failed.load()) {
        loop_result.status.success = false;
        loop_result.status.writer_error = true;
    }

    // Release pool framebuffers after render — reduces peak memory
    // from ~900 MB to ~400 MB for VPS-friendly operation.
    // The pool will reallocate on the next render if needed.
    if (session.renderer && session.renderer->framebuffer_pool()) {
        session.renderer->framebuffer_pool()->clear();
        spdlog::info("[video] Released framebuffer pool — memory trimmed");
    }

    RenderLoopOutput output;
    output.loop_result = std::move(loop_result);
    output.telemetry_frames = std::move(telemetry_frames);
    output.render_ms = profiling::duration_ms(render_t0, render_t1);
    output.render_end = render_t1;
    return output;
}

} // namespace chronon3d::cli

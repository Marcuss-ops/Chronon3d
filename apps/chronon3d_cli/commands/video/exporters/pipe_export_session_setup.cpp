#include "../common/pipe_export_session.hpp"
#include "../common/pipe_export_helpers.hpp"

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/triple_buffer_arena.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/assets/asset_preflight_resolver.hpp>


#include <spdlog/spdlog.h>
#include <filesystem>
#include <thread>
#include <memory>

namespace chronon3d::cli {

std::unique_ptr<PipeExportSession> setup_pipe_export_session(
    const CompositionRegistry& registry,
    const Composition& comp,
    const RenderSettings& settings,
    const FfmpegExportOptions& opts,
    Frame start,
    Frame end)
{
    auto session = std::make_unique<PipeExportSession>();
    session->opts = opts;
    // P1-B: atomic output — FFmpeg writes to a .partial temp file.
    // On success, make_pipe_export_result() renames it to the final path.
    // On failure, the .partial file is cleaned up.
    session->original_output_path = opts.output.output;
    session->opts.output.output += ".partial";
    session->start_frame = start;
    session->end_frame = end;
    session->canvas_width = comp.width();
    session->canvas_height = comp.height();
    session->total_frames = static_cast<int64_t>(end - start);
    session->started_at_iso =
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
        chronon3d::telemetry::TelemetryManager::get_current_iso_time();
#else
        "";
#endif

    profiling::g_live_framebuffer_bytes.store(0, std::memory_order_relaxed);
    profiling::g_peak_live_framebuffer_bytes.store(0, std::memory_order_relaxed);

    // ── Resolve codec ─────────────────────────────────────────────────────
    // Uses session->opts (not the const param opts) because output.output
    // has been modified to include the .partial suffix for atomic output.
    const bool codec_auto = session->opts.encoder.codec == "auto";
    const std::string codec = codec_auto
        ? "libx264"
        : resolve_cli_ffmpeg_codec(session->opts.encoder.codec, session->opts.encoder.hardware_encoder);

    // ── Create encoder ────────────────────────────────────────────────────
    session->encoder = create_video_encoder(session->opts);
    if (!session->encoder) {
        spdlog::error("[video] Failed to create encoder");
        return session;  // encoder is null → caller checks
    }

    // Only create output directory for sinks that actually write output
    if (session->opts.sink.sink_type == VideoSinkType::Ffmpeg ||
        session->opts.sink.sink_type == VideoSinkType::RawFile) {
        if (!ensure_output_directory_exists(session->opts.output.output)) {
            return session;
        }
    }

    auto pipe_options = make_pipe_options(comp, session->opts, codec);
    if (!session->encoder->open(pipe_options)) {
        spdlog::error("[video] Failed to open encoder");
        return session;
    }

    // Track FFmpeg process only for ffmpeg pipe sink
    if (session->opts.sink.sink_type == VideoSinkType::Ffmpeg) {
        track_pipe_encoder_process(session->opts, *session->encoder, session->sys_metrics);
    }

    // NOTE: asset mounting (CWD) is handled per-renderer inside
    // create_renderer() (cli_render_utils.cpp) which mounts CWD on both
    // renderer->runtime().assets() and renderer->runtime().resolver().
    // The old cli_asset_registry().mount(CWD) here was a redundant
    // global mutable mount removed in the P1-A refactor.

    // ── Create renderer ──────────────────────────────────────────────────
    const auto renderer_t0 = profiling::now();
    session->renderer = create_renderer(registry, settings);
    const auto renderer_t1 = profiling::now();

    if (session->renderer->counters()) {
        const auto setup_ms = static_cast<uint64_t>(
            profiling::duration_ms(renderer_t0, renderer_t1));
        session->renderer->counters()->setup_graph_parsing_ms.fetch_add(setup_ms, std::memory_order_relaxed);
    }
    // 06 R3b — `create_renderer` returns `std::shared_ptr<SoftwareRenderer>`
    // (the CLI-side type contract is now SoftwareRenderer-direct).  No
    // dynamic_cast required; the renderer pointer IS the right type.
    session->sw_renderer = session->renderer.get();

    // ── Font preflight (P0 video/text — Fase 1) ────────────────────────────
    // Check fonts referenced by the composition before rendering starts.
    // Missing fonts fail early with a clear error instead of crashing or
    // producing black frames.
    //
    // FIX #4 — Wire FontEngine into preflight evaluate().  Without the
    // engine, materialize_text_run_shape logs "no FontEngine available"
    // and returns nullptr, causing text shapes to be missing from the
    // preflight scene.  The actual render path (renderer->render())
    // correctly wires FontEngine via render_composition_frame, but the
    // preflight was calling comp.evaluate(start) without it.
    {
        Scene scene = comp.evaluate(start, 0.0f,
                                    &session->renderer->font_engine());
        auto preflight_result = AssetPreflightResolver::check(
            scene, session->renderer->runtime().resolver(),
            PreflightMode::FullComposition);
        if (!preflight_result.ok()) {
            std::string text = format_preflight_issues_text(preflight_result.issues);
            spdlog::error("[video] Asset preflight FAILED:\n{}", text);
            return session;
        }
    }

    // ── Wire counters into encoder so async converter thread can report telemetry ──
    if (session->sw_renderer && session->sw_renderer->counters()) {
        session->encoder->set_counters(session->sw_renderer->counters());

        // Record the sink type in telemetry counters (renderer must exist first)
        session->sw_renderer->counters()->video_sink_type_id.store(
            static_cast<uint64_t>(session->opts.sink.sink_type), std::memory_order_relaxed);
    }

    // ── Arena, queue ──────────────────────────────────────────────────────
    const size_t arena_size = compute_pipe_arena_size(comp.width(), comp.height());
    session->triple_arena = std::make_unique<TripleBufferArena>(4, arena_size);

    // ── Writer thread (context stored in session so it outlives the thread) ─
    auto writer_ctx = std::unique_ptr<WriterThreadContext>(
        new WriterThreadContext{
            .queue = session->queue,
            .writer_failed = session->writer_failed,
            .writer_done = session->writer_done,
            .triple_arena = *session->triple_arena,
            .encoder = *session->encoder,
            .renderer = *session->sw_renderer,
            .writer_encode_us_total = session->writer_encode_us_total,
            .frame_encoder_telemetry = session->frame_encoder_telemetry,
        });
    session->writer_thread = std::thread(run_writer_thread, std::ref(*writer_ctx));
    session->writer_ctx = std::move(writer_ctx);

    return session;
}

} // namespace chronon3d::cli

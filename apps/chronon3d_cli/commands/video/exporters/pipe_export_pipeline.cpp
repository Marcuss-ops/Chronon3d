#include "../common/pipe_export_pipeline.hpp"
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
#include <functional>
#include <memory>
#include <thread>

namespace chronon3d::cli {

std::unique_ptr<PipeExportSession> setup_pipe_export_session(
    const CompositionRegistry& registry,
    const Composition& comp,
    const RenderSettings& settings,
    const FfmpegExportOptions& opts,
    Frame start,
    Frame end,
    const chronon3d::CpuBudget& cpu_budget)
{
    // P0-1 fix(pipe): construct queue in PipeExportSession ctor.  RenderFrameQueue
    // holds std::mutex + std::condition_variable internally so it is neither
    // movable nor assignable — a late `session->queue = …` would be a build rot.
    // Queue capacity matches the in-flight arena count (4) so the render thread
    // blocks instead of busy-waiting when all arenas are queued.
    constexpr size_t kArenaPoolCount = 4;
    auto session = std::make_unique<PipeExportSession>(kArenaPoolCount);
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

    auto pipe_options = make_pipe_options(comp, session->opts, codec, cpu_budget);
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
    // Inject the single CLI CpuBudget so the runtime does not recompute it.
    Config renderer_cfg = Config::from_environment(cpu_budget);
    session->renderer = create_renderer(registry, settings, std::move(renderer_cfg));
    const auto renderer_t1 = profiling::now();

    if (session->renderer->counters()) {
        const auto setup_ms = static_cast<uint64_t>(
            profiling::duration_ms(renderer_t0, renderer_t1));
        session->renderer->counters()->setup_graph_parsing_ms.fetch_add(setup_ms, std::memory_order_relaxed);
    }
    // 06 R3b — `create_renderer` returns `std::shared_ptr<SoftwareRenderer>`
    // (the CLI-side type contract is now SoftwareRenderer-direct).  No
    // dynamic_cast required; the renderer pointer IS the right type.
    SoftwareRenderer* sw_renderer = session->renderer.get();

    // ── Font preflight (P0 video/text — Fase 1) ────────────────────────────
    // Check fonts referenced by the composition before rendering starts.
    // Missing fonts fail early with a clear error instead of crashing or
    // producing black frames.
    {
        Scene scene = evaluate_video_scene(comp, start, *sw_renderer);
        auto preflight_result = AssetPreflightResolver::check(
            scene, sw_renderer->runtime().resolver(),
            PreflightMode::FullComposition);
        if (!preflight_result.ok()) {
            std::string text = format_preflight_issues_text(preflight_result.issues);
            spdlog::error("[video] Asset preflight FAILED:\n{}", text);
            return session;
        }
    }

    // ── Wire counters into encoder so async converter thread can report telemetry ──
    if (sw_renderer && sw_renderer->counters()) {
        session->encoder->set_counters(sw_renderer->counters());

        // Record the sink type in telemetry counters (renderer must exist first)
        sw_renderer->counters()->video_sink_type_id.store(
            static_cast<uint64_t>(session->opts.sink.sink_type), std::memory_order_relaxed);
    }

    // ── Arena ──────────────────────────────────────────────────────────────
    // Note: the queue itself is owned by PipeExportSession (constructed above
    // with kArenaPoolCount as capacity).  We only allocate the arena pool here.
    const size_t arena_size = compute_pipe_arena_size(comp.width(), comp.height());
    session->triple_arena = std::make_unique<TripleBufferArena>(kArenaPoolCount, arena_size);

    // ── Writer thread (context stored in session so it outlives the thread) ─
    auto writer_ctx = std::unique_ptr<WriterThreadContext>(
        new WriterThreadContext{
            .queue = session->queue,
            .writer_failed = session->writer_failed,
            .triple_arena = *session->triple_arena,
            .encoder = *session->encoder,
            .renderer = *sw_renderer,
            .writer_encode_us_total = session->writer_encode_us_total,
            .frames_encoded = session->frames_encoded,
            .frame_encoder_telemetry = session->frame_encoder_telemetry,
        });
    session->writer_thread = std::thread(run_writer_thread, std::ref(*writer_ctx));
    session->writer_ctx = std::move(writer_ctx);

    return session;
}

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

namespace {

// Text composition warm-up bundles — pre-allocates size classes used by the
// MinimalistText family so the first frames don't stall on allocation and
// pool exact-hit rate climbs from ~55% to >80% on text-heavy pipelines.
//
//   1920x900  — text-bbox ROI with glow padding (cinem-white radius ~50px).
//               Used by `apply_downsample_blur` clip-bounded regions and by
//               the GlowPipeline ROI accumulator in `build_glow_accumulator`.
//   480x270   — downsample-half heuristic at 4× scale (1920/4 × 1080/4).
//               Used by `BlurStrategy::DownsampleQuarter` for radius > 24.
//
// Both are best-fit reuse candidates when the geometric bbox is small (e.g.
// "FADE UP" centered in a 1920×1080 canvas → ROI is ~1920×900 with side
// margins). Pre-warming them gives exact-hit + best-fit reuse instead of
// fresh allocations on the hot EffectStack path.
void warmup_text_size_classes(cache::FramebufferPool& pool) {
    struct TextSizeClass { int w; int h; size_t count; const char* label; };
    // Counts tuned against the FramebufferPool default budget (384 MB) so the
    // total preallocation stays well under the cap. Color is float4 = 16 B/px,
    // so e.g. the canvas bucket (1920×1152) costs ~35 MB per buffer. The
    // chosen counts deliver ~273 MB pre-warmed and leave headroom for free
    // allocations during the actual render.
    const TextSizeClass layout[] = {
        {.w = 1920, .h = 900,  .count = 3, .label = "text-bbox+glow-pad"},
        {.w = 960,  .h = 540,  .count = 3, .label = "downsample-half"},
        {.w = 480,  .h = 270,  .count = 3, .label = "downsample-quarter"},
    };
    for (const auto& cls : layout) {
        const auto [bw, bh] = cache::FramebufferPool::round_to_bucket(cls.w, cls.h);
        const auto n = pool.preallocate(cache::FramebufferPoolPreallocOptions{
            .width = bw,
            .height = bh,
            .count = cls.count,
            .clear = true,
            .touch_memory = false,
        });
        if (n > 0) {
            spdlog::info("[pool-warm] Pre-allocated {} buffers ({}) bucket {}x{} at startup",
                         n, cls.label, bw, bh);
        }
    }
}

} // namespace

void warmup_pipe_pool(PipeExportSession& session) {
    if (!session.renderer || !session.renderer->framebuffer_pool()) {
        return;
    }

    const auto [bw, bh] = cache::FramebufferPool::round_to_bucket(
        session.canvas_width, session.canvas_height);
    const auto prealloced = session.renderer->framebuffer_pool()->preallocate(
        cache::FramebufferPoolPreallocOptions{
            .width = bw,
            .height = bh,
            .count = 4,
            .clear = true,
            .touch_memory = false,
        });
    if (prealloced > 0) {
        spdlog::info("[pool-warm] Pre-allocated {} canvas buffers ({}x{} bucket) at startup",
                     prealloced, bw, bh);
    }

    // Pre-warm the text-composition ROI + downsample size classes.
    warmup_text_size_classes(*session.renderer->framebuffer_pool());
}

} // namespace chronon3d::cli

#include "../common/video_export_common.hpp"
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/telemetry/telemetry_bundle.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/assets/asset_preflight_resolver.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <algorithm>
#include <fmt/format.h>

namespace chronon3d::cli {

namespace {
bool is_shell_safe(const std::string& s) {
    return s.find_first_of(";|&$`\\\"'<>{}()!") == std::string::npos;
}
} // namespace

ChunkedExportResult render_and_encode_ffmpeg_chunked(
    const CompositionRegistry& registry,
    const Composition& comp,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts,
    const chronon3d::CpuBudget& cpu_budget)
{
    ChunkedExportResult result;
    result.frames_total = static_cast<int>(end - start);

    profiling::g_live_framebuffer_bytes.store(0, std::memory_order_relaxed);
    profiling::g_peak_live_framebuffer_bytes.store(0, std::memory_order_relaxed);

    const int total = result.frames_total;
    const std::filesystem::path frames_dir = std::filesystem::temp_directory_path() / opts.output.frames_dir_name;
    std::error_code ec;
    std::filesystem::create_directories(frames_dir, ec);
    if (ec) {
        spdlog::error("[video] Cannot create frames dir {}: {}", frames_dir.string(), ec.message());
        return result; // return_code=1, success=false (defaults)
    }

    int chunks = std::max(1, std::min(opts.sink.chunks, total));

    spdlog::info("[video] Rendering {} frames [{}, {}) at {} fps in {} chunks → {}",
                 total, start, end, opts.output.fps, chunks, opts.output.output);

    const auto started_at_iso =
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
        chronon3d::telemetry::TelemetryManager::get_current_iso_time();
#else
        std::string{};
#endif

    // ── Font preflight (P0 video/text — Fase 1) ────────────────────────────
    // Create a temporary renderer just for preflight check.
    // Font preflight uses the canonical evaluate_video_scene() which
    // threads FontEngine into composition evaluation.
    {
        Config preflight_cfg = Config::from_environment(cpu_budget);
        auto preflight_renderer = create_renderer(registry, settings, std::move(preflight_cfg));
        Scene scene = evaluate_video_scene(comp, start, *preflight_renderer);
        auto preflight_result = AssetPreflightResolver::check(
            scene, preflight_renderer->runtime().resolver(),
            PreflightMode::FullComposition);
        if (!preflight_result.ok()) {
            std::string text = format_preflight_issues_text(preflight_result.issues);
            spdlog::error("[video] Asset preflight FAILED:\n{}", text);
            return result;
        }
    }

    const auto wall_t0 = profiling::now();
    const auto setup_t0 = wall_t0;
    chronon3d::RenderCounters aggregate_counters{};
    std::mutex aggregate_mutex;
    std::vector<chronon3d::telemetry::NodeTelemetryRecord> node_events;
    std::vector<chronon3d::telemetry::LayerTelemetryRecord> layer_events;
    std::vector<chronon3d::telemetry::CacheTelemetryRecord> cache_events;
    std::vector<chronon3d::telemetry::CullingTelemetryRecord> culling_events;
    std::vector<chronon3d::telemetry::TextTelemetryRecord> text_events;
    std::vector<chronon3d::telemetry::ImageTelemetryRecord> image_events;
    std::vector<chronon3d::telemetry::TileTelemetryRecord> tile_events;
    std::mutex telemetry_data_mutex;
    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;
    std::mutex frames_mutex;

    auto ranges = split_frame_range(start, end, chunks);
    const auto setup_done_at = profiling::now();
    const auto render_t0 = setup_done_at;
    std::atomic<bool> failed{false};
    std::atomic<int> frames_done{0};
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(chunks));

    for (const auto& chunk : ranges) {
        workers.emplace_back([&, chunk]() {
            try {
                const auto renderer_t0 = profiling::now();
                Config chunk_cfg = Config::from_environment(cpu_budget);
                auto renderer = create_renderer(registry, settings, std::move(chunk_cfg));
                const auto renderer_t1 = profiling::now();
                if (renderer->counters()) {
                    const auto setup_ms = static_cast<uint64_t>(
                        profiling::duration_ms(renderer_t0, renderer_t1));
                    renderer->counters()->setup_graph_parsing_ms.fetch_add(setup_ms, std::memory_order_relaxed);
                    // Note: in chunked mode each worker creates its own renderer, so
                    // setup_graph_parsing_ms will be summed across all workers in the aggregate.
                }

                // Warmup
                uint64_t saved_fb_alloc = 0;
                uint64_t saved_fb_reuses = 0;
                uint64_t saved_fb_bytes = 0;
                uint64_t saved_fb_peak = 0;
                if (opts.warmup.warmup_renderer) {
                    const auto warmup_t0 = profiling::now();
                    runtime::warmup_renderer(*renderer, comp, runtime::RendererWarmupOptions{
                        .width = comp.width(),
                        .height = comp.height(),
                        .framebuffer_count = opts.warmup.warmup_framebuffers,
                        .preallocate_framebuffers = true,
                        .touch_memory = true,
                        .render_dummy_frame = opts.warmup.warmup_dummy_frame,
                        .dummy_frame = 0,
                        .quiet = false,
                    });
                    const auto warmup_t1 = profiling::now();

                    if (renderer->counters()) {
                        const auto warmup_ms = static_cast<uint64_t>(
                            profiling::duration_ms(warmup_t0, warmup_t1));
                        renderer->counters()->setup_pool_preallocation_ms.fetch_add(warmup_ms, std::memory_order_relaxed);
                        // Note: in chunked mode each worker runs its own warmup, so
                        // setup_pool_preallocation_ms will be summed across all workers in the aggregate.

                        saved_fb_alloc = renderer->counters()->framebuffer_allocations.load(std::memory_order_relaxed);
                        saved_fb_reuses = renderer->counters()->framebuffer_reuses.load(std::memory_order_relaxed);
                        saved_fb_bytes = renderer->counters()->framebuffer_bytes_allocated.load(std::memory_order_relaxed);
                        saved_fb_peak = renderer->counters()->framebuffer_bytes_peak.load(std::memory_order_relaxed);
                    }

                    renderer->counters()->reset();

                    if (renderer->counters()) {
                        renderer->counters()->framebuffer_allocations.store(saved_fb_alloc, std::memory_order_relaxed);
                        renderer->counters()->framebuffer_reuses.store(saved_fb_reuses, std::memory_order_relaxed);
                        renderer->counters()->framebuffer_bytes_allocated.store(saved_fb_bytes, std::memory_order_relaxed);
                        renderer->counters()->framebuffer_bytes_peak.store(saved_fb_peak, std::memory_order_relaxed);
                    }

                    // Clear per-event telemetry stores after warmup, since
                    // atomic counters were reset above.  This keeps Hot Nodes
                    // events in sync with atomic counters.
                    chronon3d::telemetry::clear_telemetry_stores();
                }

                std::vector<chronon3d::telemetry::FrameTelemetryRecord> local_frames;
                local_frames.reserve(static_cast<size_t>(chunk.end - chunk.start));
                for (Frame f = chunk.start; f < chunk.end; ++f) {
                    if (failed.load()) return;
                    const auto frame_t0 = profiling::now();
                    const auto hits_before = renderer->node_cache().stats().hits;
                    auto fb = renderer->render(comp, f);
                    const auto hits_after_render = renderer->node_cache().stats().hits;
                    const double dirty_ratio = renderer->last_dirty_area_ratio();
                    if (!fb) {
                        spdlog::error("[video] Render failed at frame {}", f);
                        failed.store(true);
                        return;
                    }
                    const auto frame_render_t1 = profiling::now();
                    const auto png = (frames_dir / fmt::format("frame_{:06d}.png", f - start)).string();
                    if (!save_png(*fb, png)) {
                        spdlog::error("[video] PNG write failed: {}", png);
                        failed.store(true);
                        return;
                    }
                    const auto frame_t1 = profiling::now();
                    const double render_ms = profiling::duration_ms(frame_t0, frame_render_t1);
                    const double encode_ms = profiling::duration_ms(frame_render_t1, frame_t1);
                    local_frames.push_back({
                        .frame_number = static_cast<int>(f),
                        .duration_ms = profiling::duration_ms(frame_t0, frame_t1),
                        .cache_hit = (hits_after_render > hits_before),
                        .dirty_area_ratio = dirty_ratio,
                        .graph_eval_ms = render_ms,
                        .encoder_ms = encode_ms,
                        .program_cache_capacity = static_cast<int>(
                            renderer->counters()
                                ? renderer->counters()->program_cache_capacity.load(std::memory_order_relaxed)
                                : 0)
                    });
                    
                    int done = ++frames_done;
                    if (done % std::max(1, total / 10) == 0 || done == total) {
                        spdlog::info("[video]   {}/{} frames", done, total);
                    }
                }

                std::lock_guard<std::mutex> lock(aggregate_mutex);
                cli::telemetry::add_counters(aggregate_counters, *renderer->counters());
                
                auto local_telemetry = chronon3d::telemetry::collect_all_telemetry();

                {
                    std::lock_guard<std::mutex> tel_lock(telemetry_data_mutex);
                    for (auto& ev : local_telemetry.node_events) node_events.push_back(std::move(ev));
                    for (auto& ev : local_telemetry.layer_events) layer_events.push_back(std::move(ev));
                    for (auto& ev : local_telemetry.cache_events) cache_events.push_back(std::move(ev));
                    for (auto& ev : local_telemetry.culling_events) culling_events.push_back(std::move(ev));
                    for (auto& ev : local_telemetry.text_events) text_events.push_back(std::move(ev));
                    for (auto& ev : local_telemetry.image_events) image_events.push_back(std::move(ev));
                    for (auto& ev : local_telemetry.tile_events) tile_events.push_back(std::move(ev));
                }
                {
                    std::lock_guard<std::mutex> frames_lock(frames_mutex);
                    telemetry_frames.insert(telemetry_frames.end(), local_frames.begin(), local_frames.end());
                }
            } catch (const std::exception& e) {
                spdlog::error("[video] Exception in render worker for chunk [{}, {}): {}", chunk.start, chunk.end, e.what());
                failed.store(true);
            } catch (...) {
                spdlog::error("[video] Unknown exception in render worker for chunk [{}, {})", chunk.start, chunk.end);
                failed.store(true);
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    const auto render_t1 = profiling::now();
    const auto setup_t1 = setup_done_at;

    result.chunk_failed = failed.load();
    if (result.chunk_failed) {
        spdlog::error("[video] Chunked render failed");
    }

    result.frames_written = frames_done.load();
    bool success = !result.chunk_failed;

    if (success) {
        const auto output_parent = std::filesystem::path(opts.output.output).parent_path();
        if (!output_parent.empty()) {
            std::filesystem::create_directories(output_parent, ec);
            if (ec) {
                spdlog::error("[video] Cannot create output directory {}: {}", output_parent.string(), ec.message());
                success = false;
                result.encode_failed = true;  // pre-encode setup failure
            }
        }

        if (success) {
            const std::string pattern = (frames_dir / "frame_%06d.png").string();
            const std::string codec   = resolve_cli_ffmpeg_codec(opts.encoder.codec, opts.encoder.hardware_encoder);
            const std::string pix_fmt = resolve_cli_ffmpeg_output_pix_fmt(codec);
            const std::string cmd     = fmt::format(
                "ffmpeg -y -framerate {} -i \"{}\" -c:v {} -crf {} -preset {} -pix_fmt {} -movflags +faststart \"{}\"",
                opts.output.fps, pattern, codec, opts.encoder.crf, opts.encoder.encode_preset, pix_fmt, opts.output.output);

            spdlog::info("[video] {}", cmd);
            const auto encode_t0 = profiling::now();
            const int rc = [&]() {
                if (!is_shell_safe(opts.encoder.encode_preset) || !is_shell_safe(opts.output.output)) {
                    spdlog::error("[video] encode_preset or output path contains shell metacharacters, refusing to execute");
                    result.encode_failed = true;
                    return -1;
                }
                return std::system(cmd.c_str());
            }();
            const auto encode_t1 = profiling::now();
            result.encode_ms = profiling::duration_ms(encode_t0, encode_t1);

            if (rc != 0) {
                spdlog::error("[video] ffmpeg exited with code {}", rc);
                success = false;
                result.encode_failed = true;
            }
        }
    }

    if (!opts.sink.keep_frames) {
        std::filesystem::remove_all(frames_dir, ec);
    }

    const auto wall_t1 = profiling::now();
    result.render_ms = profiling::duration_ms(render_t0, render_t1);
    result.wall_time_ms = profiling::duration_ms(wall_t0, wall_t1);
    result.success = success;
    result.return_code = success ? 0 : 1;

    const double render_ms = result.render_ms;
    const double wall_time_ms = result.wall_time_ms;
    const double encode_ms = result.encode_ms;
    const int frames_written = result.frames_written;

    std::sort(telemetry_frames.begin(), telemetry_frames.end(),
              [](const auto& a, const auto& b) { return a.frame_number < b.frame_number; });
    auto phases = std::vector<chronon3d::telemetry::PhaseTelemetryRecord>{
        {"setup_renderer", profiling::duration_ms(setup_t0, setup_t1)},
        {"rendering_loop", profiling::duration_ms(render_t0, render_t1)},
        {"encoder_close_and_flush", encode_ms},
    };
    auto graph_phases = cli::telemetry::capture_graph_phase_records(aggregate_counters);
    phases.insert(phases.begin() + 1, graph_phases.begin(), graph_phases.end());
    // On failure, report 0 written frames to avoid misleading telemetry
    // where frames_written=total but the video encode (ffmpeg) failed.
    const int encoded_frames = success ? frames_written : 0;

    // ── Compute render artifact (P0 video/text — Fase 1) ────────────────────
    std::vector<chronon3d::telemetry::RenderArtifactRecord> artifacts;
    {
        namespace fs = std::filesystem;
        const std::string out_path = opts.output.output;
        if (!out_path.empty()) {
            chronon3d::telemetry::RenderArtifactRecord artifact;
            artifact.type = "video";
            artifact.path = out_path;
            std::error_code ec;
            artifact.file_exists = fs::exists(out_path, ec);
            if (artifact.file_exists) {
                artifact.size_bytes = static_cast<int64_t>(fs::file_size(out_path, ec));
                if (ec) artifact.size_bytes = 0;
            }
            artifacts.push_back(artifact);
        }
    }

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
    cli::telemetry::record_output_run(
        /*composition_id=*/composition_id,
        /*output_path=*/opts.output.output,
        /*success=*/success,
        /*frames_total=*/total,
        /*frames_written=*/encoded_frames,
        /*wall_time_ms=*/wall_time_ms,
        /*render_ms=*/render_ms,
        /*encode_ms=*/encode_ms,
        /*started_at_iso=*/started_at_iso,
        /*phases=*/phases,
        /*counters=*/telemetry::capture_counters(aggregate_counters),
        /*node_events=*/node_events,
        /*counters_src=*/&aggregate_counters,
        /*frames=*/telemetry_frames,
        /*layer_events=*/layer_events,
        /*cache_events=*/cache_events,
        /*culling_events=*/culling_events,
        /*text_events=*/text_events,
        /*image_events=*/image_events,
        /*tile_events=*/tile_events,
        /*artifacts=*/artifacts);
#endif

    if (!success) {
        return result;
    }

    spdlog::info("[video] Done → {}", opts.output.output);
    return result;
}

} // namespace chronon3d::cli

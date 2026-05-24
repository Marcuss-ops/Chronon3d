#include "video_export_common.hpp"
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/framebuffer_arena.hpp>
#include <chronon3d/core/triple_buffer_arena.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/render_pipeline.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <filesystem>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <concurrentqueue/moodycamel/concurrentqueue.h>
#include <memory>
#include <mutex>
#include <vector>
#include <algorithm>

namespace chronon3d::cli {

struct RenderFramePackage {
    std::shared_ptr<Framebuffer> framebuffer;
    std::shared_ptr<FramebufferArena> arena;
};

int render_and_encode_ffmpeg_pipe(
    const CompositionRegistry& registry,
    const Composition& comp,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts)
{
    profiling::g_live_framebuffer_bytes.store(0, std::memory_order_relaxed);
    profiling::g_peak_live_framebuffer_bytes.store(0, std::memory_order_relaxed);

    const int total = static_cast<int>(end - start);
    const auto started_at_iso = chronon3d::telemetry::TelemetryManager::get_current_iso_time();
    const auto wall_t0 = std::chrono::steady_clock::now();
    const auto setup_t0 = wall_t0;
    double render_ms = 0.0;
    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;

    if (opts.chunks != 1) {
        spdlog::warn("[video] --chunks is ignored with --ffmpeg-mode pipe in V1");
    }

    const std::string codec = resolve_cli_ffmpeg_codec(opts.codec, opts.hardware_encoder);

    FfmpegPipeEncoder pipe;
    FfmpegPipeOptions pipe_options{
        .width = comp.width(),
        .height = comp.height(),
        .fps = opts.fps,
        .crf = opts.crf,
        .preset = opts.encode_preset,
        .codec = codec,
        .output_path = opts.output,
        .input_format = parse_pipe_pixfmt(opts.pipe_pixfmt),
        .verbose = opts.ffmpeg_verbose,
        .color_transform = {
            .output = parse_color_output(opts.color_output),
        },
        .pipe_writer = opts.pipe_writer,
    };

    std::error_code ec;
    const auto output_parent = std::filesystem::path(opts.output).parent_path();
    if (!output_parent.empty()) {
        std::filesystem::create_directories(output_parent, ec);
        if (ec) {
            spdlog::error("[video] Cannot create output directory {}: {}", output_parent.string(), ec.message());
            return 1;
        }
    }

    if (!pipe.open(pipe_options)) {
        spdlog::error("[video] Failed to open FFmpeg raw pipe");
        return 1;
    }

    auto renderer = create_renderer(registry, settings);
    SoftwareRenderer* sw_renderer = dynamic_cast<SoftwareRenderer*>(renderer.get());
    
    // Node cache for RenderGraph
    graph::NodeCache node_cache;
    std::shared_ptr<VideoDecoderFactory> video_decoder = nullptr;

    // P1: Initialize Triple Buffering Arena (increased to 20 for extreme decoupling)
    const size_t arena_size = 3584ULL * 1024ULL * 1024ULL; // 3.5 GB
    TripleBufferArena triple_arena(20, arena_size);

    // Moodycamel lock-free queue for high-throughput frame passing
    moodycamel::ConcurrentQueue<RenderFramePackage> queue;
    std::atomic<bool> writer_failed{false};
    std::atomic<bool> writer_done{false};
    double queue_wait_ms_total = 0.0;

    auto writer_thread_fn = [&]() {
        profiling::g_current_counters = renderer->counters();
        profiling::g_current_framebuffer_pool = renderer->framebuffer_pool().get();
        bool arena_notified = false;
        for (;;) {
            RenderFramePackage package;
            if (!queue.try_dequeue(package)) {
                if (writer_done.load()) {
                    if (!queue.try_dequeue(package)) break;
                } else if (writer_failed.load()) {
                    break;
                } else {
                    std::this_thread::yield();
                    continue;
                }
            }

            if (package.framebuffer) {
                if (!arena_notified) {
                    spdlog::info(\"[video] Exporting via Arena-backed SIMD pipeline\");
                    arena_notified = true;
                }
                const auto t_conv0 = std::chrono::high_resolution_clock::now();
                if (!pipe.write_frame(*package.framebuffer)) {
                    writer_failed.store(true);
                    return;
                }
                const auto t_conv1 = std::chrono::high_resolution_clock::now();
                renderer->counters()->frame_conversion_copy_ms.fetch_add(
                    static_cast<uint64_t>(std::chrono::duration<double, std::milli>(t_conv1 - t_conv0).count()),
                    std::memory_order_relaxed);
            }
            
            triple_arena.release(package.arena);
            package.framebuffer.reset();
        }
    };

    std::thread writer_thread(writer_thread_fn);

    RenderSettings render_opts = settings;
    render_opts.use_modular_graph = true;

    // Warmup
    if (opts.warmup_renderer) {
        runtime::warmup_renderer(*renderer, comp, runtime::RendererWarmupOptions{
            .width = comp.width(),
            .height = comp.height(),
            .framebuffer_count = opts.warmup_framebuffers,
            .preallocate_framebuffers = true,
            .touch_memory = true,
            .render_dummy_frame = opts.warmup_dummy_frame,
            .dummy_frame = 0,
            .quiet = false
        });
        renderer->trace()->clear();
        renderer->counters()->reset();
    }

    const auto render_t0 = std::chrono::steady_clock::now();
    Frame current_frame = start;
    try {
        for (; current_frame < end; ++current_frame) {
            if (writer_failed.load()) {
                spdlog::error("[video] FFmpeg writer failed before frame {}", current_frame);
                break;
            }

            auto current_arena = triple_arena.acquire();
            if (sw_renderer) {
                sw_renderer->framebuffer_pool()->set_arena(current_arena.get());
            }

            const auto frame_t0 = std::chrono::steady_clock::now();
            
            auto fb = render_composition_frame(
                *renderer, node_cache, comp, current_frame, render_opts, registry, video_decoder);
            
            const auto frame_t1 = std::chrono::steady_clock::now();
            const double dirty_ratio = sw_renderer ? sw_renderer->last_dirty_area_ratio() : 1.0;

            if (!fb) {
                spdlog::error("[video] Failed to render frame {}", current_frame);
                triple_arena.release(current_arena);
                break;
            }

            // High-throughput enqueue
            const auto wait_t0 = std::chrono::steady_clock::now();
            while (queue.size_approx() > 16) {
                if (writer_failed.load()) break;
                std::this_thread::yield();
            }
            queue.enqueue({std::move(fb), std::move(current_arena)});
            const auto wait_t1 = std::chrono::steady_clock::now();
            queue_wait_ms_total += std::chrono::duration<double, std::milli>(wait_t1 - wait_t0).count();

            telemetry_frames.push_back({
                .frame_number = static_cast<int>(current_frame),
                .duration_ms = std::chrono::duration<double, std::milli>(frame_t1 - frame_t0).count(),
                .cache_hit = true,
                .dirty_area_ratio = dirty_ratio
            });
        }
    } catch (const std::exception& e) {
        spdlog::error("[video] Exception during render loop (frame {}): {}", current_frame, e.what());
    }

    const auto render_t1 = std::chrono::steady_clock::now();
    writer_done.store(true);
    if (writer_thread.joinable()) {
        writer_thread.join();
    }
    const auto setup_t1 = render_t0;
    auto node_events = chronon3d::telemetry::collect_node_telemetry();
    auto layer_events = chronon3d::telemetry::collect_layer_telemetry();
    auto cache_events = chronon3d::telemetry::collect_cache_telemetry();
    auto culling_events = chronon3d::telemetry::collect_culling_telemetry();
    auto text_events = chronon3d::telemetry::collect_text_telemetry();
    auto image_events = chronon3d::telemetry::collect_image_telemetry();
    auto tile_events = chronon3d::telemetry::collect_tile_telemetry();

    const double write_blocked_ms = pipe.total_write_blocked_ms();
    spdlog::info("[video] FFmpeg pipe write blocked duration: {:.2f} ms", write_blocked_ms);
    spdlog::info("[video_diag] conversion_and_copy_duration_ms: {} ms", renderer->counters()->frame_conversion_copy_ms.load());
    spdlog::info("[video] FFmpeg queue wait duration: {:.2f} ms", queue_wait_ms_total);

    if (!pipe.close()) {
        spdlog::error("[video] FFmpeg pipe encoder failed");
        return 1;
    }

    const auto wall_t1 = std::chrono::steady_clock::now();
    render_ms = std::chrono::duration<double, std::milli>(render_t1 - render_t0).count();
    const double encode_ms = std::chrono::duration<double, std::milli>(wall_t1 - render_t1).count();
    const double wall_time_ms = std::chrono::duration<double, std::milli>(wall_t1 - wall_t0).count();
    std::sort(telemetry_frames.begin(), telemetry_frames.end(),
              [](const auto& a, const auto& b) { return a.frame_number < b.frame_number; });
    const auto phases = std::vector<chronon3d::telemetry::PhaseTelemetryRecord>{
        {"setup_renderer", std::chrono::duration<double, std::milli>(setup_t1 - setup_t0).count()},
        {"rendering_loop", std::chrono::duration<double, std::milli>(render_t1 - render_t0).count()},
        {"encoding", encode_ms},
    };
    auto resolved_counters = telemetry::capture_counters(*renderer->counters());
    resolved_counters.push_back({"ffmpeg_pipe_write_blocked_duration_ms", static_cast<uint64_t>(std::llround(write_blocked_ms))});
    resolved_counters.push_back({"ffmpeg_queue_wait_duration_ms", static_cast<uint64_t>(std::llround(queue_wait_ms_total))});

    cli::telemetry::record_output_run(
        /*composition_id=*/composition_id,
        /*output_path=*/opts.output,
        /*success=*/true,
        /*frames_total=*/total,
        /*frames_written=*/total,
        /*wall_time_ms=*/wall_time_ms,
        /*render_ms=*/render_ms,
        /*encode_ms=*/encode_ms,
        /*started_at_iso=*/started_at_iso,
        /*phases=*/phases,
        /*counters=*/resolved_counters,
        /*node_events=*/node_events,
        /*counters_src=*/renderer->counters(),
        /*frames=*/telemetry_frames,
        /*layer_events=*/layer_events,
        /*cache_events=*/cache_events,
        /*culling_events=*/culling_events,
        /*text_events=*/text_events,
        /*image_events=*/image_events,
        /*tile_events=*/tile_events);

    spdlog::info("[video] Wrote {}", opts.output);
    return 0;
}

} // namespace chronon3d::cli

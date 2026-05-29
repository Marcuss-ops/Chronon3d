#include "video_export_common.hpp"
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/framebuffer_arena.hpp>
#include <chronon3d/core/triple_buffer_arena.hpp>
#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/system_metrics.hpp>
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

namespace {

constexpr size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

}

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

    const bool codec_auto = opts.codec == "auto";
    const std::string codec = codec_auto ? "libx264" : resolve_cli_ffmpeg_codec(opts.codec, opts.hardware_encoder);

    auto encoder = create_video_encoder(opts);
    chronon3d::SystemMetricsCollector sys_metrics;
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
    pipe_options.output_pix_fmt = resolve_cli_ffmpeg_output_pix_fmt(codec);

    std::error_code ec;
    const auto output_parent = std::filesystem::path(opts.output).parent_path();
    if (!output_parent.empty()) {
        std::filesystem::create_directories(output_parent, ec);
        if (ec) {
            spdlog::error("[video] Cannot create output directory {}: {}", output_parent.string(), ec.message());
            return 1;
        }
    }

    if (!encoder->open(pipe_options)) {
        spdlog::error("[video] Failed to open encoder");
        return 1;
    }

    // Track FFmpeg child process for CPU% monitoring (pipe mode only)
    if (opts.encoder_backend != "native") {
        auto* pipe_enc = dynamic_cast<FfmpegPipeEncoder*>(encoder.get());
        if (pipe_enc) {
            sys_metrics.track_ffmpeg_pid(pipe_enc->ffmpeg_pid());
            if (pipe_enc->ffmpeg_pid() > 0) {
                spdlog::info("[video] Tracking FFmpeg child PID {} for system metrics", pipe_enc->ffmpeg_pid());
            }
        }
    }

    auto renderer = create_renderer(registry, settings);
    SoftwareRenderer* sw_renderer = dynamic_cast<SoftwareRenderer*>(renderer.get());
    
    cache::NodeCache node_cache;
    video::VideoFrameDecoder* video_decoder = nullptr;

    // Keep the arena proportional to the actual frame size instead of pinning
    // a large fixed slab for every export.
    const size_t frame_bytes = static_cast<size_t>(comp.width()) * static_cast<size_t>(comp.height()) * sizeof(Color);
    const size_t arena_size = align_up(std::max<size_t>(32ULL * 1024ULL * 1024ULL, frame_bytes * 2ULL), 2ULL * 1024ULL * 1024ULL);
    TripleBufferArena triple_arena(8, arena_size);

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
            const auto pop_t0 = std::chrono::steady_clock::now();
            bool was_idle = false;
            while (!queue.try_dequeue(package)) {
                if (writer_done.load()) {
                    if (!queue.try_dequeue(package)) break;
                } else if (writer_failed.load()) {
                    break;
                } else {
                    was_idle = true;
                    std::this_thread::yield();
                    continue;
                }
            }
            if (writer_failed.load() || (writer_done.load() && !package.framebuffer)) break;

            const auto pop_t1 = std::chrono::steady_clock::now();
            const uint64_t dequeue_ms = static_cast<uint64_t>(
                std::chrono::duration<double, std::milli>(pop_t1 - pop_t0).count());
            if (renderer->counters()) {
                renderer->counters()->io_queue_pop_wait_ms.fetch_add(dequeue_ms, std::memory_order_relaxed);
                if (was_idle) {
                    renderer->counters()->io_writer_idle_wait_ms.fetch_add(dequeue_ms, std::memory_order_relaxed);
                }
            }

            if (package.framebuffer) {
                if (!arena_notified) {
                    spdlog::info("[video] Exporting via Arena-backed SIMD pipeline");
                    arena_notified = true;
                }
                if (!encoder->write_frame(*package.framebuffer)) {
                    writer_failed.store(true);
                    return;
                }
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
        renderer->counters()->reset();
    }

    const auto render_t0 = std::chrono::steady_clock::now();
    Frame current_frame = start;
    try {
        for (; current_frame < end; ++current_frame) {
            // Check for graceful cancellation (SIGINT/SIGTERM)
            if (opts.cancellation_token && opts.cancellation_token->is_cancelled()) {
                spdlog::warn("[video] Render cancelled at frame {}", current_frame);
                break;
            }

            if (writer_failed.load()) {
                spdlog::error("[video] FFmpeg writer failed before frame {}", current_frame);
                break;
            }

            int done_count = static_cast<int>(current_frame - start + 1);
            if (done_count % std::max(1, total / 10) == 0 || done_count == total) {
                spdlog::info("[video]   {}/{} frames", done_count, total);
            }

            auto current_arena = triple_arena.acquire();
            if (sw_renderer) {
                sw_renderer->framebuffer_pool()->set_arena(current_arena);
            }

            const auto frame_t0 = std::chrono::steady_clock::now();
            fprintf(stderr, "[PERF] frame=%d t0=%.3f\n", (int)current_frame, 
                std::chrono::duration<double>(frame_t0.time_since_epoch()).count());
            fflush(stderr);
            
            auto fb = render_composition_frame(
                *renderer, node_cache, render_opts, &registry, video_decoder, comp, current_frame);
            
            const auto frame_t1 = std::chrono::steady_clock::now();
            const double frame_ms = std::chrono::duration<double, std::milli>(frame_t1 - frame_t0).count();
            fprintf(stderr, "[PERF] frame=%d render_ms=%.2f\n", (int)current_frame, frame_ms);
            fflush(stderr);
            if (renderer->counters()) {
                renderer->counters()->video_graph_eval_ms.fetch_add(
                    static_cast<uint64_t>(frame_ms),
                    std::memory_order_relaxed);
            }
            const double dirty_ratio = sw_renderer ? sw_renderer->last_dirty_area_ratio() : 1.0;

            if (!fb) {
                spdlog::error("[video] Failed to render frame {}", current_frame);
                triple_arena.release(current_arena);
                break;
            }

            // High-throughput enqueue
            const auto wait_t0 = std::chrono::steady_clock::now();
            const auto q_size = queue.size_approx();
            if (renderer->counters()) {
                auto current_peak = renderer->counters()->io_queue_peak_depth.load(std::memory_order_relaxed);
                while (q_size > current_peak && !renderer->counters()->io_queue_peak_depth.compare_exchange_weak(current_peak, q_size, std::memory_order_relaxed));
            }

            while (queue.size_approx() > 64) {
                if (writer_failed.load()) break;
                std::this_thread::yield();
            }
            queue.enqueue(RenderFramePackage{std::move(fb), std::move(current_arena)});
            const auto wait_t1 = std::chrono::steady_clock::now();
            const double wait_ms = std::chrono::duration<double, std::milli>(wait_t1 - wait_t0).count();
            queue_wait_ms_total += wait_ms;
            if (renderer->counters()) {
                renderer->counters()->io_queue_push_blocked_ms.fetch_add(
                    static_cast<uint64_t>(wait_ms),
                    std::memory_order_relaxed);
            }

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

    const bool is_native = (opts.encoder_backend == "native");

    const double write_blocked_ms = [&]() -> double {
        if (is_native) return 0.0;
        auto* pipe_enc = dynamic_cast<FfmpegPipeEncoder*>(encoder.get());
        if (pipe_enc) return pipe_enc->total_write_blocked_ms();
        return 0.0;
    }();

    // Native encoder telemetry
    const double native_convert_ms     = encoder->native_convert_ms();
    const double native_send_ms       = encoder->native_send_frame_ms();
    const double native_receive_ms    = encoder->native_receive_packet_ms();
    const double native_mux_ms        = encoder->native_mux_write_ms();
    const double native_trailer_ms    = encoder->native_trailer_ms();

    const double conv_copy_ms = static_cast<double>(renderer->counters()->frame_conversion_copy_ms.load());
    spdlog::info("[video] Encoder write blocked duration: {:.2f} ms", write_blocked_ms);
    spdlog::info("[video_diag] conversion_and_copy_duration_ms: {} ms", conv_copy_ms);
    spdlog::info("[video] FFmpeg queue wait duration: {:.2f} ms", queue_wait_ms_total);
    if (is_native) {
        spdlog::info("[video_native] convert={:.2f}ms  send_frame={:.2f}ms  receive_packet={:.2f}ms  mux_write={:.2f}ms  trailer={:.2f}ms",
                     native_convert_ms, native_send_ms, native_receive_ms, native_mux_ms, native_trailer_ms);
    }

    if (!encoder->close()) {
        spdlog::error("[video] Encoder failed");
        return 1;
    }

    const auto wall_t1 = std::chrono::steady_clock::now();
    render_ms = std::chrono::duration<double, std::milli>(render_t1 - render_t0).count();
    const double encode_ms = std::chrono::duration<double, std::milli>(wall_t1 - render_t1).count();
    const double wall_time_ms = std::chrono::duration<double, std::milli>(wall_t1 - wall_t0).count();

    // ── Benchmark breakdown calculations ──
    const double chronon_render_only_ms = render_ms - queue_wait_ms_total;
    const double chronon_queue_wait_ms = queue_wait_ms_total;
    const double chronon_conversion_copy_ms = conv_copy_ms;
    const double ffmpeg_encode_total_ms = write_blocked_ms;
    const double ffmpeg_flush_close_ms = std::chrono::duration<double, std::milli>(wall_t1 - render_t1).count();

    // Store per-run total counters for benchmark separation
    if (renderer->counters()) {
        if (is_native) {
            renderer->counters()->native_av_convert_ms.store(
                static_cast<uint64_t>(native_convert_ms), std::memory_order_relaxed);
            renderer->counters()->native_av_send_frame_ms.store(
                static_cast<uint64_t>(native_send_ms), std::memory_order_relaxed);
            renderer->counters()->native_av_receive_packet_ms.store(
                static_cast<uint64_t>(native_receive_ms), std::memory_order_relaxed);
            renderer->counters()->native_av_mux_write_ms.store(
                static_cast<uint64_t>(native_mux_ms), std::memory_order_relaxed);
            renderer->counters()->native_av_trailer_ms.store(
                static_cast<uint64_t>(native_trailer_ms), std::memory_order_relaxed);
        } else {
            renderer->counters()->video_pipe_write_ms.store(
                static_cast<uint64_t>(write_blocked_ms), std::memory_order_relaxed);
            renderer->counters()->ffmpeg_pipe_write_blocked_ms.store(
                static_cast<uint64_t>(write_blocked_ms), std::memory_order_relaxed);
            renderer->counters()->ffmpeg_flush_ms.store(
                static_cast<uint64_t>(ffmpeg_flush_close_ms), std::memory_order_relaxed);
            renderer->counters()->video_ffmpeg_latency_ms.store(
                static_cast<uint64_t>(write_blocked_ms), std::memory_order_relaxed);
        }
    }

    spdlog::info("[benchmark_chronon] render_only={:.2f}ms  conv_copy={:.2f}ms  queue_wait={:.2f}ms  throughput={:.2f}ms",
                 chronon_render_only_ms, chronon_conversion_copy_ms, chronon_queue_wait_ms,
                 chronon_render_only_ms + chronon_queue_wait_ms);

    if (is_native) {
        spdlog::info("[benchmark_e2e] native_convert={:.2f}ms  native_send={:.2f}ms  native_receive={:.2f}ms  native_mux={:.2f}ms  native_trailer={:.2f}ms  wall={:.2f}ms",
                     native_convert_ms, native_send_ms, native_receive_ms, native_mux_ms, native_trailer_ms, wall_time_ms);
    } else {
        spdlog::info("[benchmark_e2e] ffmpeg_encode={:.2f}ms  ffmpeg_flush_close={:.2f}ms  wall={:.2f}ms",
                     ffmpeg_encode_total_ms, ffmpeg_flush_close_ms, wall_time_ms);
    }

    std::sort(telemetry_frames.begin(), telemetry_frames.end(),
              [](const auto& a, const auto& b) { return a.frame_number < b.frame_number; });

    // Build phase records — include native telemetry when applicable
    std::vector<chronon3d::telemetry::PhaseTelemetryRecord> phases;
    phases.push_back({"setup_renderer", std::chrono::duration<double, std::milli>(setup_t1 - setup_t0).count()});
    phases.push_back({"rendering_loop", std::chrono::duration<double, std::milli>(render_t1 - render_t0).count()});
    phases.push_back({"encoding", encode_ms});
    phases.push_back({"chronon_render_only_ms", chronon_render_only_ms});
    phases.push_back({"chronon_conversion_copy_ms", chronon_conversion_copy_ms});
    phases.push_back({"chronon_queue_wait_ms", chronon_queue_wait_ms});
    if (is_native) {
        phases.push_back({"native_av_convert_ms", native_convert_ms});
        phases.push_back({"native_av_send_frame_ms", native_send_ms});
        phases.push_back({"native_av_receive_packet_ms", native_receive_ms});
        phases.push_back({"native_av_mux_write_ms", native_mux_ms});
        phases.push_back({"native_av_trailer_ms", native_trailer_ms});
    } else {
        phases.push_back({"ffmpeg_encode_total_ms", ffmpeg_encode_total_ms});
        phases.push_back({"ffmpeg_flush_close_ms", ffmpeg_flush_close_ms});
    }
    phases.push_back({"e2e_wall_ms", wall_time_ms});
    auto resolved_counters = telemetry::capture_counters(*renderer->counters());
    resolved_counters.push_back({"ffmpeg_pipe_write_blocked_duration_ms", static_cast<uint64_t>(std::llround(write_blocked_ms))});
    resolved_counters.push_back({"ffmpeg_queue_wait_duration_ms", static_cast<uint64_t>(std::llround(queue_wait_ms_total))});

    // Collect system metrics (FFmpeg CPU%, page faults, context switches, LLC counters)
    // and store them into the renderer's counters for telemetry.
    if (renderer->counters()) {
        sys_metrics.fill_system_counters(*renderer->counters());
    }

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

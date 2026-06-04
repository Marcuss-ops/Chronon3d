#include "video_export_common.hpp"
#include "pipe_export_helpers.hpp"
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/triple_buffer_arena.hpp>
#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/render_pipeline.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <concurrentqueue/moodycamel/concurrentqueue.h>
#include <memory>
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
    double render_graph_eval_ms_total = 0.0;  // solo render_composition_frame(), escluso queue/encoder
    std::atomic<uint64_t> writer_encode_us_total{0}; // tempo di encoding nel writer thread (atomic, us)
    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;

    if (opts.chunks != 1) {
        spdlog::warn("[video] --chunks is ignored with --ffmpeg-mode pipe in V1");
    }

    const bool codec_auto = opts.codec == "auto";
    const std::string codec = codec_auto ? "libx264" : resolve_cli_ffmpeg_codec(opts.codec, opts.hardware_encoder);

    auto encoder = create_video_encoder(opts);
    chronon3d::SystemMetricsCollector sys_metrics;
    FfmpegPipeOptions pipe_options = make_pipe_options(comp, opts, codec);

    if (!ensure_output_directory_exists(opts.output)) {
        return 1;
    }

    if (!encoder->open(pipe_options)) {
        spdlog::error("[video] Failed to open encoder");
        return 1;
    }

    track_pipe_encoder_process(opts, *encoder, sys_metrics);

    const auto renderer_t0 = std::chrono::steady_clock::now();
    auto renderer = create_renderer(registry, settings);
    const auto renderer_t1 = std::chrono::steady_clock::now();
    if (renderer->counters()) {
        const auto setup_ms = static_cast<uint64_t>(
            std::chrono::duration<double, std::milli>(renderer_t1 - renderer_t0).count());
        renderer->counters()->setup_graph_parsing_ms.fetch_add(setup_ms, std::memory_order_relaxed);
    }
    SoftwareRenderer* sw_renderer = dynamic_cast<SoftwareRenderer*>(renderer.get());
    
    cache::NodeCache node_cache;
    video::VideoFrameDecoder* video_decoder = nullptr;

    // Keep the arena proportional to the actual frame size instead of pinning
    // a large fixed slab for every export.
    const size_t arena_size = compute_pipe_arena_size(comp.width(), comp.height());
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
                const auto enc_t0 = std::chrono::steady_clock::now();
                if (!encoder->write_frame(*package.framebuffer)) {
                    writer_failed.store(true);
                    return;
                }
                const auto enc_t1 = std::chrono::steady_clock::now();
                const uint64_t enc_us = static_cast<uint64_t>(
                    std::chrono::duration<double, std::micro>(enc_t1 - enc_t0).count());
                writer_encode_us_total.fetch_add(enc_us, std::memory_order_relaxed);
            }
            
            triple_arena.release(package.arena);
            package.framebuffer.reset();
        }
    };

    std::thread writer_thread(writer_thread_fn);

    RenderSettings render_opts = settings;
    render_opts.use_modular_graph = true;

    warmup_pipe_renderer(*renderer, comp, opts);

    bool success = true;
    bool cancelled = false;
    bool render_failed = false;
    bool writer_error = false;
    bool exception_error = false;
    int frames_written = 0;

    const auto render_t0 = std::chrono::steady_clock::now();
    Frame current_frame = start;
    try {
        for (; current_frame < end; ++current_frame) {
            // Check for graceful cancellation (SIGINT/SIGTERM)
            if (opts.cancellation_token && opts.cancellation_token->is_cancelled()) {
                spdlog::warn("[video] Render cancelled at frame {}", current_frame);
                success = false;
                cancelled = true;
                break;
            }

            if (writer_failed.load()) {
                spdlog::error("[video] FFmpeg writer failed before frame {}", current_frame);
                success = false;
                writer_error = true;
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
            auto fb = render_composition_frame(
                *renderer, node_cache, render_opts, &registry, video_decoder, comp, current_frame);
            const auto frame_t1 = std::chrono::steady_clock::now();
            const double frame_ms = std::chrono::duration<double, std::milli>(frame_t1 - frame_t0).count();
            render_graph_eval_ms_total += frame_ms;
            if (renderer->counters()) {
                renderer->counters()->video_graph_eval_ms.fetch_add(
                    static_cast<uint64_t>(frame_ms),
                    std::memory_order_relaxed);
            }
            const double dirty_ratio = sw_renderer ? sw_renderer->last_dirty_area_ratio() : 1.0;

            if (!fb) {
                spdlog::error("[video] Failed to render frame {}", current_frame);
                triple_arena.release(current_arena);
                success = false;
                render_failed = true;
                break;
            }

            // High-throughput enqueue
            const auto wait_t0 = std::chrono::steady_clock::now();
            const auto q_size = queue.size_approx();
            if (renderer->counters()) {
                auto current_peak = renderer->counters()->io_queue_peak_depth.load(std::memory_order_relaxed);
                while (q_size > current_peak && !renderer->counters()->io_queue_peak_depth.compare_exchange_weak(current_peak, q_size, std::memory_order_relaxed));
            }

            // Allow deeper queue before back-pressure kicks in.
            // With kMaxEagainRetries=12 in the native encoder, the writer
            // can drain more frames before blocking the render thread.
            while (queue.size_approx() > 128) {
                if (writer_failed.load()) {
                    success = false;
                    writer_error = true;
                    break;
                }
                std::this_thread::yield();
            }

            if (writer_error) {
                triple_arena.release(current_arena);
                break;
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
            ++frames_written;

            telemetry_frames.push_back({
                .frame_number = static_cast<int>(current_frame),
                .duration_ms = frame_ms,
                .cache_hit = true,
                .dirty_area_ratio = dirty_ratio
            });
        }
    } catch (const std::exception& e) {
        spdlog::error("[video] Exception during render loop (frame {}): {}", current_frame, e.what());
        success = false;
        exception_error = true;
    }

    const auto render_t1 = std::chrono::steady_clock::now();
    writer_done.store(true);
    if (writer_thread.joinable()) {
        writer_thread.join();
    }
    if (writer_failed.load()) {
        success = false;
        writer_error = true;
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

    const double write_blocked_ms = pipe_write_blocked_ms(is_native, *encoder);

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

    bool encoder_closed = encoder->close();
    if (!encoder_closed) {
        spdlog::error("[video] Encoder failed");
        success = false;
    }

    const auto wall_t1 = std::chrono::steady_clock::now();
    render_ms = std::chrono::duration<double, std::milli>(render_t1 - render_t0).count();
    const double encode_ms = std::chrono::duration<double, std::milli>(wall_t1 - render_t1).count();
    const double wall_time_ms = std::chrono::duration<double, std::milli>(wall_t1 - wall_t0).count();

    // ── Benchmark breakdown calculations ──
    // render_ms = tempo totale dal render_t0 al render_t1 (include render + queue blocking + writer join)
    // render_graph_eval_ms_total = solo render_composition_frame() — il vero rendering
    // queue_wait_ms_total = tempo bloccati sulla coda piena (aspettando writer)
    // writer_encode_ms = tempo speso nel writer thread (encoding) — atomic, convertito da us
    const double writer_encode_ms = static_cast<double>(writer_encode_us_total.load(std::memory_order_relaxed)) / 1000.0;
    const double chronon_render_pure_ms = render_graph_eval_ms_total;
    // render_only = solo il vero rendering, come da PR 7 correzione
    const double chronon_render_only_ms = render_graph_eval_ms_total;
    const double chronon_render_loop_ms = render_ms; // tempo totale del loop (include queue wait + writer join)
    const double chronon_queue_wait_ms = queue_wait_ms_total;
    const double chronon_writer_encode_ms = writer_encode_ms;
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

    spdlog::info("[benchmark_chronon] render_pure={:.2f}ms  render_only={:.2f}ms  render_loop={:.2f}ms  conv_copy={:.2f}ms  queue_wait={:.2f}ms  writer_encode={:.2f}ms  throughput={:.2f}ms",
                 chronon_render_pure_ms, chronon_render_only_ms, chronon_render_loop_ms,
                 chronon_conversion_copy_ms, chronon_queue_wait_ms, chronon_writer_encode_ms,
                 chronon_render_pure_ms + chronon_queue_wait_ms);

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
    phases.push_back({"encoder_close_and_flush", encode_ms});
    phases.push_back({"chronon_render_pure_ms", chronon_render_pure_ms});
    phases.push_back({"chronon_render_only_ms", chronon_render_only_ms});
    phases.push_back({"chronon_render_loop_ms", chronon_render_loop_ms});
    phases.push_back({"chronon_conversion_copy_ms", chronon_conversion_copy_ms});
    phases.push_back({"chronon_queue_wait_ms", chronon_queue_wait_ms});
    phases.push_back({"chronon_writer_encode_ms", chronon_writer_encode_ms});
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

    // Framebuffer pool stats (captured after warmup, before render loop consumed buffers)
    if (sw_renderer && sw_renderer->framebuffer_pool()) {
        auto pool_stats = sw_renderer->framebuffer_pool()->stats();
        resolved_counters.push_back({"framebuffer_pool_capacity", pool_stats.max_bytes});
        resolved_counters.push_back({"framebuffer_pool_available_count", pool_stats.available_count});
        resolved_counters.push_back({"framebuffer_pool_current_bytes", pool_stats.current_bytes});
        resolved_counters.push_back({"framebuffer_pool_total_allocations", pool_stats.total_allocations});
        resolved_counters.push_back({"framebuffer_pool_total_reuses", pool_stats.total_reuses});
    }

    // Collect system metrics (FFmpeg CPU%, page faults, context switches, LLC counters)
    // and store them into the renderer's counters for telemetry.
    if (renderer->counters()) {
        sys_metrics.fill_system_counters(*renderer->counters());
    }

    // On failure, report 0 written frames to avoid misleading telemetry
    // where frames_written=total but the export was cancelled or failed.
    const int encoded_frames = success ? frames_written : 0;
    cli::telemetry::record_output_run(
        /*composition_id=*/composition_id,
        /*output_path=*/opts.output,
        /*success=*/success,
        /*frames_total=*/total,
        /*frames_written=*/encoded_frames,
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

    if (!success) {
        log_pipe_export_failure(cancelled, render_failed, writer_error, exception_error);
        return 1;
    }

    spdlog::info("[video] Wrote {}", opts.output);
    return 0;
}

} // namespace chronon3d::cli

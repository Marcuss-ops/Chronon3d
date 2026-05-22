#include "video_export_common.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <filesystem>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
#include <algorithm>

namespace chronon3d::cli {

int render_and_encode_ffmpeg_pipe(
    const CompositionRegistry& registry,
    const Composition& comp,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts)
{
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
    constexpr size_t kMaxQueuedFrames = 8;
    std::deque<std::shared_ptr<Framebuffer>> pending_frames;
    std::mutex pending_mutex;
    std::condition_variable pending_cv;
    std::condition_variable space_cv;
    std::atomic<bool> writer_failed{false};
    bool writer_done = false;
    double queue_wait_ms_total = 0.0;

    auto writer_thread_fn = [&]() {
        for (;;) {
            std::shared_ptr<Framebuffer> fb;
            {
                std::unique_lock<std::mutex> lock(pending_mutex);
                pending_cv.wait(lock, [&]() {
                    return writer_done || !pending_frames.empty() || writer_failed.load();
                });

                if (writer_failed.load()) {
                    return;
                }

                if (pending_frames.empty()) {
                    if (writer_done) {
                        break;
                    }
                    continue;
                }

                fb = std::move(pending_frames.front());
                pending_frames.pop_front();
                space_cv.notify_one();
            }

            if (!pipe.write_frame(*fb)) {
                writer_failed.store(true);
                pending_cv.notify_all();
                space_cv.notify_all();
                return;
            }
        }
    };

    std::thread writer_thread(writer_thread_fn);

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
                {
                    std::lock_guard<std::mutex> lock(pending_mutex);
                    writer_done = true;
                }
                pending_cv.notify_all();
                space_cv.notify_all();
                if (writer_thread.joinable()) {
                    writer_thread.join();
                }
                pipe.close();
                return 1;
            }

            const auto frame_t0 = std::chrono::steady_clock::now();
            const auto hits_before = renderer->node_cache().stats().hits;
            auto fb = renderer->render_frame(comp, current_frame);
            const auto hits_after_render = renderer->node_cache().stats().hits;
            const double dirty_ratio = renderer->last_dirty_area_ratio();
            const auto frame_t1 = std::chrono::steady_clock::now();
            if (!fb) {
                spdlog::error("[video] Failed to render frame {}", current_frame);
                {
                    std::lock_guard<std::mutex> lock(pending_mutex);
                    writer_done = true;
                }
                pending_cv.notify_all();
                space_cv.notify_all();
                if (writer_thread.joinable()) {
                    writer_thread.join();
                }
                pipe.close();
                return 1;
            }

            {
                std::unique_lock<std::mutex> lock(pending_mutex);
                space_cv.wait(lock, [&]() {
                    return writer_failed.load() || pending_frames.size() < kMaxQueuedFrames;
                });
                if (writer_failed.load()) {
                    spdlog::error("[video] FFmpeg writer failed while queueing frame {}", current_frame);
                    writer_done = true;
                    lock.unlock();
                    pending_cv.notify_all();
                    space_cv.notify_all();
                    if (writer_thread.joinable()) {
                        writer_thread.join();
                    }
                    pipe.close();
                    return 1;
                }
                pending_frames.push_back(std::move(fb));
            }
            pending_cv.notify_one();
            const auto queue_t1 = std::chrono::steady_clock::now();
            queue_wait_ms_total += std::chrono::duration<double, std::milli>(queue_t1 - frame_t1).count();

            telemetry_frames.push_back({
                .frame_number = static_cast<int>(current_frame),
                .duration_ms = std::chrono::duration<double, std::milli>(frame_t1 - frame_t0).count(),
                .cache_hit = (hits_after_render > hits_before),
                .dirty_area_ratio = dirty_ratio
            });
        }
    } catch (const std::exception& e) {
        spdlog::error("[video] Exception during render loop (frame {}): {}", current_frame, e.what());
        {
            std::lock_guard<std::mutex> lock(pending_mutex);
            writer_done = true;
        }
        pending_cv.notify_all();
        space_cv.notify_all();
        if (writer_thread.joinable()) {
            writer_thread.join();
        }
        pipe.close();
        return 1;
    } catch (...) {
        spdlog::error("[video] Unknown exception during render loop at frame {}", current_frame);
        {
            std::lock_guard<std::mutex> lock(pending_mutex);
            writer_done = true;
        }
        pending_cv.notify_all();
        space_cv.notify_all();
        if (writer_thread.joinable()) {
            writer_thread.join();
        }
        pipe.close();
        return 1;
    }

    const auto render_t1 = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(pending_mutex);
        writer_done = true;
    }
    pending_cv.notify_all();
    space_cv.notify_all();
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

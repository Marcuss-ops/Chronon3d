#include "../commands.hpp"
#include "../utils/cli_render_utils.hpp"
#include "../utils/cli_mappers.hpp"
#include "../utils/frame_chunks.hpp"
#include "../utils/ffmpeg_pipe_encoder.hpp"
#include "../utils/telemetry_run.hpp"
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/render_telemetry.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>
#include <chronon3d/presets/camera_motion_clip.hpp>
#include <chronon3d/runtime/renderer_warmup.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <filesystem>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <algorithm>

namespace chronon3d::cli {

namespace {

bool ffmpeg_in_path() {
#ifdef _WIN32
    return std::system("ffmpeg -version > NUL 2>&1") == 0;
#else
    return std::system("ffmpeg -version > /dev/null 2>&1") == 0;
#endif
}

PipePixelFormat parse_pipe_pixfmt(const std::string& fmt) {
    if (fmt == "yuv420p") return PipePixelFormat::YUV420P;
    if (fmt == "nv12")    return PipePixelFormat::NV12;
    return PipePixelFormat::RGBA;
}

color::ColorSpace parse_color_output(const std::string& cs) {
    if (cs == "rec709") return color::ColorSpace::Rec709;
    if (cs == "linearsrgb") return color::ColorSpace::LinearSRGB;
    return color::ColorSpace::SRGB;
}

std::string resolve_cli_ffmpeg_codec(const std::string& codec, const std::string& hw_encoder) {
    if (codec != "auto") return codec;
    if (hw_encoder == "nvenc") return "h264_nvenc";
    if (hw_encoder == "qsv") return "h264_qsv";
    if (hw_encoder == "videotoolbox" || hw_encoder == "vt") return "h264_videotoolbox";
    if (hw_encoder == "amf") return "h264_amf";
    return "libx264";
}

struct FfmpegExportOptions {
    std::string output;
    std::string frames_dir_name;
    int fps;
    int crf;
    std::string codec;
    std::string hardware_encoder;
    std::string encode_preset;
    bool keep_frames;
    int chunks;
    std::string ffmpeg_mode{"png"};
    bool ffmpeg_verbose{false};
    std::string pipe_pixfmt{"rgba"};
    std::string color_output{"srgb"};

    // Renderer warmup
    bool   warmup_renderer{false};
    size_t warmup_framebuffers{16};
    bool   warmup_dummy_frame{false};
};

int render_and_encode_ffmpeg(
    const CompositionRegistry& registry,
    const Composition& comp,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts)
{
    if (opts.output.empty()) {
        spdlog::error("[video] No output path specified.");
        return 1;
    }
    if (!ffmpeg_in_path()) {
        spdlog::error("[video] ffmpeg not found in PATH.");
        return 1;
    }

    if (end <= start) {
        spdlog::error("[video] Empty frame range [{}, {})", start, end);
        return 1;
    }

    if (opts.ffmpeg_mode != "png" && opts.ffmpeg_mode != "pipe") {
        spdlog::error("[video] Unknown --ffmpeg-mode '{}'. Expected: png, pipe", opts.ffmpeg_mode);
        return 1;
    }

    const int total = static_cast<int>(end - start);

    if (opts.ffmpeg_mode == "pipe") {
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
                const auto frame_t0 = std::chrono::steady_clock::now();
                const auto hits_before = renderer->node_cache().stats().hits;
                auto fb = renderer->render_frame(comp, current_frame);
                const auto hits_after_render = renderer->node_cache().stats().hits;
                const double dirty_ratio = renderer->last_dirty_area_ratio();
                if (!fb) {
                    spdlog::error("[video] Failed to render frame {}", current_frame);
                    pipe.close();
                    return 1;
                }

                if (!pipe.write_frame(*fb)) {
                    spdlog::error("[video] Failed to write frame {} to FFmpeg pipe", current_frame);
                    pipe.close();
                    return 1;
                }
                const auto frame_t1 = std::chrono::steady_clock::now();

                telemetry_frames.push_back({
                    .frame_number = static_cast<int>(current_frame),
                    .duration_ms = std::chrono::duration<double, std::milli>(frame_t1 - frame_t0).count(),
                    .cache_hit = (hits_after_render > hits_before),
                    .dirty_area_ratio = dirty_ratio
                });
            }
        } catch (const std::exception& e) {
            spdlog::error("[video] Exception during render loop (frame {}): {}", current_frame, e.what());
            pipe.close();
            return 1;
        } catch (...) {
            spdlog::error("[video] Unknown exception during render loop at frame {}", current_frame);
            pipe.close();
            return 1;
        }
        const auto render_t1 = std::chrono::steady_clock::now();
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

        if (!pipe.close()) {
            spdlog::error("[video] FFmpeg pipe encoder failed");
            return 1;
        }

        const auto wall_t1 = std::chrono::steady_clock::now();
        render_ms = std::chrono::duration<double, std::milli>(render_t1 - render_t0).count();
        const double wall_time_ms = std::chrono::duration<double, std::milli>(wall_t1 - wall_t0).count();
        std::sort(telemetry_frames.begin(), telemetry_frames.end(),
                  [](const auto& a, const auto& b) { return a.frame_number < b.frame_number; });
        const auto phases = std::vector<chronon3d::telemetry::PhaseTelemetryRecord>{
            {"setup_renderer", std::chrono::duration<double, std::milli>(setup_t1 - setup_t0).count()},
            {"rendering_loop", std::chrono::duration<double, std::milli>(render_t1 - render_t0).count()},
            {"encoding", std::chrono::duration<double, std::milli>(wall_t1 - render_t1).count()},
        };
        auto resolved_counters = telemetry::capture_counters(*renderer->counters());
        resolved_counters.push_back({"ffmpeg_pipe_write_blocked_duration_ms", static_cast<uint64_t>(std::llround(write_blocked_ms))});

        cli::telemetry::record_output_run(
            /*composition_id=*/composition_id,
            /*output_path=*/opts.output,
            /*success=*/true,
            /*frames_total=*/total,
            /*frames_written=*/total,
            /*wall_time_ms=*/wall_time_ms,
            /*render_ms=*/render_ms,
            /*encode_ms=*/write_blocked_ms,
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

    const std::filesystem::path frames_dir = std::filesystem::temp_directory_path() / opts.frames_dir_name;
    std::error_code ec;
    std::filesystem::create_directories(frames_dir, ec);
    if (ec) {
        spdlog::error("[video] Cannot create frames dir {}: {}", frames_dir.string(), ec.message());
        return 1;
    }

    int chunks = std::max(1, std::min(opts.chunks, total));

    spdlog::info("[video] Rendering {} frames [{}, {}) at {} fps in {} chunks → {}",
                 total, start, end, opts.fps, chunks, opts.output);

    const auto started_at_iso = chronon3d::telemetry::TelemetryManager::get_current_iso_time();
    const auto wall_t0 = std::chrono::steady_clock::now();
    const auto setup_t0 = wall_t0;
    chronon3d::RenderCounters aggregate_counters{};
    std::mutex aggregate_mutex;
    std::vector<chronon3d::telemetry::NodeTelemetryRecord> node_events;
    std::mutex node_events_mutex;
    std::vector<chronon3d::telemetry::FrameTelemetryRecord> telemetry_frames;
    std::mutex frames_mutex;

    auto ranges = split_frame_range(start, end, chunks);
    const auto setup_done_at = std::chrono::steady_clock::now();
    const auto render_t0 = setup_done_at;
    std::atomic<bool> failed{false};
    std::atomic<int> frames_done{0};
    std::vector<std::thread> workers;

    for (const auto& chunk : ranges) {
        workers.emplace_back([&, chunk]() {
            try {
                auto renderer = create_renderer(registry, settings);

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

                std::vector<chronon3d::telemetry::FrameTelemetryRecord> local_frames;
                local_frames.reserve(static_cast<size_t>(chunk.end - chunk.start));
                for (Frame f = chunk.start; f < chunk.end; ++f) {
                    if (failed.load()) return;
                    const auto frame_t0 = std::chrono::steady_clock::now();
                    const auto hits_before = renderer->node_cache().stats().hits;
                    auto fb = renderer->render_frame(comp, f);
                    const auto hits_after_render = renderer->node_cache().stats().hits;
                    const double dirty_ratio = renderer->last_dirty_area_ratio();
                    if (!fb) {
                        spdlog::error("[video] Render failed at frame {}", f);
                        failed.store(true);
                        return;
                    }
                    const auto png = (frames_dir / fmt::format("frame_{:06d}.png", f - start)).string();
                    if (!save_png(*fb, png)) {
                        spdlog::error("[video] PNG write failed: {}", png);
                        failed.store(true);
                        return;
                    }
                    const auto frame_t1 = std::chrono::steady_clock::now();
                    local_frames.push_back({
                        .frame_number = static_cast<int>(f),
                        .duration_ms = std::chrono::duration<double, std::milli>(frame_t1 - frame_t0).count(),
                        .cache_hit = (hits_after_render > hits_before),
                        .dirty_area_ratio = dirty_ratio
                    });
                    
                    int done = ++frames_done;
                    if (done % std::max(1, total / 10) == 0 || done == total) {
                        spdlog::info("[video]   {}/{} frames", done, total);
                    }
                }

                std::lock_guard<std::mutex> lock(aggregate_mutex);
                cli::telemetry::add_counters(aggregate_counters, *renderer->counters());
                auto local_node_events = chronon3d::telemetry::collect_node_telemetry();
                if (!local_node_events.empty()) {
                    std::lock_guard<std::mutex> node_lock(node_events_mutex);
                    node_events.insert(node_events.end(),
                                       std::make_move_iterator(local_node_events.begin()),
                                       std::make_move_iterator(local_node_events.end()));
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

    const auto render_t1 = std::chrono::steady_clock::now();
    const auto setup_t1 = setup_done_at;

    if (failed.load()) {
        spdlog::error("[video] Chunked render failed");
        return 1;
    }

    const auto output_parent = std::filesystem::path(opts.output).parent_path();
    if (!output_parent.empty()) {
        std::filesystem::create_directories(output_parent, ec);
        if (ec) {
            spdlog::error("[video] Cannot create output directory {}: {}", output_parent.string(), ec.message());
            return 1;
        }
    }

    const std::string pattern = (frames_dir / "frame_%06d.png").string();
    const std::string codec   = resolve_cli_ffmpeg_codec(opts.codec, opts.hardware_encoder);
    const std::string cmd     = fmt::format(
        "ffmpeg -y -framerate {} -i \"{}\" -c:v {} -crf {} -preset {} -pix_fmt yuv420p \"{}\"",
        opts.fps, pattern, codec, opts.crf, opts.encode_preset, opts.output);

    spdlog::info("[video] {}", cmd);
    const auto encode_t0 = std::chrono::steady_clock::now();
    const int rc = std::system(cmd.c_str());
    const auto encode_t1 = std::chrono::steady_clock::now();

    if (!opts.keep_frames) {
        std::filesystem::remove_all(frames_dir, ec);
    }

    if (rc != 0) {
        spdlog::error("[video] ffmpeg exited with code {}", rc);
        return 1;
    }

    const auto wall_t1 = std::chrono::steady_clock::now();
    const double render_ms = std::chrono::duration<double, std::milli>(render_t1 - render_t0).count();
    const double encode_ms = std::chrono::duration<double, std::milli>(encode_t1 - encode_t0).count();
    const double wall_time_ms = std::chrono::duration<double, std::milli>(wall_t1 - wall_t0).count();
    std::sort(telemetry_frames.begin(), telemetry_frames.end(),
              [](const auto& a, const auto& b) { return a.frame_number < b.frame_number; });
    const auto phases = std::vector<chronon3d::telemetry::PhaseTelemetryRecord>{
        {"setup_renderer", std::chrono::duration<double, std::milli>(setup_t1 - setup_t0).count()},
        {"rendering_loop", std::chrono::duration<double, std::milli>(render_t1 - render_t0).count()},
        {"encoding", encode_ms},
    };
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
        /*counters=*/telemetry::capture_counters(aggregate_counters),
        /*node_events=*/node_events,
        /*counters_src=*/&aggregate_counters,
        /*frames=*/telemetry_frames);

    spdlog::info("[video] Done → {}", opts.output);
    return 0;
}

} // namespace

int command_video(const CompositionRegistry& registry, const VideoArgs& args) {
    if (args.comp_id.empty()) {
        spdlog::error("[video] No composition specified.");
        return 1;
    }

    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) return 1;
    const auto& comp = *resolved.comp;

    RenderSettings settings = settings_from_args(args, !resolved.from_specscene);

    FfmpegExportOptions opts;
    opts.output = args.output;
    opts.frames_dir_name = args.frames_dir.empty() ? ("chronon_" + args.comp_id) : args.frames_dir;
    opts.fps = args.fps;
    opts.crf = args.crf;
    opts.codec = args.codec;
    opts.hardware_encoder = args.hardware_encoder;
    opts.encode_preset = args.encode_preset;
    opts.keep_frames = args.keep_frames;
    opts.chunks = args.chunks;
    opts.ffmpeg_mode = args.ffmpeg_mode;
    opts.ffmpeg_verbose = args.ffmpeg_verbose;
    opts.pipe_pixfmt = args.pipe_pixfmt;
    opts.color_output = args.color_output;
    opts.warmup_renderer = args.pipeline.warmup_renderer;
    opts.warmup_framebuffers = args.pipeline.warmup_framebuffers;
    opts.warmup_dummy_frame = args.pipeline.warmup_dummy_frame;

    const Frame end = (args.end > args.start) ? args.end : comp.duration();
    
    return render_and_encode_ffmpeg(registry, comp, args.comp_id, settings, args.start, end, opts);
}

int command_video_camera(const CompositionRegistry& registry, const VideoCameraArgs& args) {
    auto axis = parse_motion_axis(args.axis);
    if (!axis) {
        spdlog::error("Unknown camera axis '{}'. Expected Tilt, Pan, or Roll.", args.axis);
        return 1;
    }

    std::string output = args.output.empty() ? 
        "output/camera_" + lower_copy(args.axis) + "_video.mp4" : args.output;

    RenderSettings settings = settings_from_args(args);

    animation::CameraMotionParams params;
    params.axis = *axis;
    if (*axis == animation::MotionAxis::Roll) {
        params.start_deg = args.roll_start_deg;
        params.end_deg = args.roll_end_deg;
    }
    params.duration = args.end - args.start;
    params.start_frame = args.start;
    params.width = args.width;
    params.height = args.height;
    params.reference_image = args.reference_image;
    params.pose.position = params.position;
    params.pose.zoom = params.zoom;

    auto comp = chronon3d::presets::camera_motion_clip("CameraTestPattern", params,
        [](SceneBuilder& s, const FrameContext& ctx, const animation::CameraMotionParams& p) {
            const f32 inset_x = static_cast<f32>(ctx.width) * 0.06f;
            const f32 inset_y = static_cast<f32>(ctx.height) * 0.06f;
            const Vec2 image_size{
                static_cast<f32>(ctx.width) - inset_x * 2.0f,
                static_cast<f32>(ctx.height) - inset_y * 2.0f,
            };
            const Vec3 image_pos{
                static_cast<f32>(ctx.width) * 0.5f,
                static_cast<f32>(ctx.height) * 0.5f,
                0.0f,
            };

            scene::utils::dark_grid_background(s, ctx);

            s.layer("reference-image", [reference_image = p.reference_image, image_size, image_pos](LayerBuilder& l) {
                l.enable_3d()
                 .image("grid_reference", {
                     .path = reference_image,
                     .size = image_size,
                     .pos = image_pos,
                     .opacity = 1.0f,
                 });
            });
        });

    FfmpegExportOptions opts;
    opts.output = output;
    opts.frames_dir_name = "chronon_camera_" + lower_copy(args.axis);
    opts.fps = args.fps;
    opts.crf = args.crf;
    opts.codec = args.codec;
    opts.hardware_encoder = args.hardware_encoder;
    opts.encode_preset = args.encode_preset;
    opts.keep_frames = false; // default for camera motion
    opts.chunks = 1; // can't easily chunk here without extending args, default 1
    opts.ffmpeg_mode = "png";
    opts.ffmpeg_verbose = false;

    return render_and_encode_ffmpeg(registry, comp, comp.name(), settings, args.start, args.end, opts);
}

} // namespace chronon3d::cli

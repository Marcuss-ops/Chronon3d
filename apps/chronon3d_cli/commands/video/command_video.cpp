#include "video_export_common.hpp"
#include "exporter_registry.hpp"
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>
#include <chronon3d/presets/camera_motion_clip.hpp>
#include <chronon3d/runtime/renderer_warmup.hpp>
#include <chronon3d/render_graph/render_pipeline.hpp>
#include "../../utils/common/cli_utils.hpp"
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/render_graph/render_pipeline.hpp>
#include <filesystem>

namespace chronon3d::cli {

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

    if (opts.encoder_backend == "native" && opts.ffmpeg_mode != "pipe") {
        spdlog::error("[video] --encoder-backend native requires --ffmpeg-mode pipe");
        return 1;
    }

    static ExporterRegistry exporter_registry = []() {
        ExporterRegistry reg;
        register_builtin_exporters(reg);
        return reg;
    }();

    auto* exporter = exporter_registry.find(opts.ffmpeg_mode);
    if (!exporter) {
        spdlog::error("[video] Unknown ffmpeg-mode '{}'. Expected one of: pipe, png", opts.ffmpeg_mode);
        return 1;
    }

    const VideoExportJob job{
        .registry = registry,
        .comp = comp,
        .composition_id = composition_id,
        .settings = settings,
        .start = start,
        .end = end,
        .opts = opts,
    };
    return exporter->export_video(job);
}

int command_video(const CompositionRegistry& registry, const VideoArgs& args) {
    if (args.comp_id.empty()) {
        spdlog::error("[video] No composition specified.");
        return 1;
    }

    const std::string video_output = args.output.empty()
        ? chronon_artifact_path("videos", std::filesystem::path(args.comp_id).filename().string() + ".mp4").string()
        : args.output;

    // ── Dry-run: validate everything but don't render ──
    if (args.dry_run) {
        auto resolved = resolve_composition(registry, args.comp_id);
        if (!resolved) return 1;
        const auto& comp = *resolved.comp;

        RenderSettings settings = settings_from_args(args, !resolved.from_specscene);
        // Dry-run: CLI end is inclusive, convert to exclusive for internal use
        const Frame end = (args.end > args.start) ? (args.end + 1) : comp.duration();
        const int total = static_cast<int>(end - args.start);

        spdlog::info("[dry-run] Composition: {}", args.comp_id);
        spdlog::info("[dry-run]   Resolution: {}×{}", comp.width(), comp.height());
        spdlog::info("[dry-run]   Frame range: {} – {} ({} frames)", args.start, end, total);
        spdlog::info("[dry-run]   FPS: {}", args.fps);
        spdlog::info("[dry-run]   Duration: {:.1f}s", static_cast<double>(total) / args.fps);
        spdlog::info("[dry-run]   Output: {}", video_output);
        spdlog::info("[dry-run]   FFmpeg mode: {}", args.ffmpeg_mode);
        spdlog::info("[dry-run]   SSAA: {}×", settings.ssaa_factor);

        // Try to build the render graph to detect errors early
        try {
            auto renderer = create_renderer(registry, settings);
            auto* sw_renderer = dynamic_cast<SoftwareRenderer*>(renderer.get());
            if (sw_renderer) {
                spdlog::info("[dry-run]   Backend: SoftwareRenderer");
            }
            cache::NodeCache node_cache;
            auto fb = chronon3d::graph::render_composition_frame(
                *renderer, node_cache, settings, &registry, nullptr, comp, args.start);
            if (!fb) {
                spdlog::warn("[dry-run]   First frame render returned null");
            } else {
                spdlog::info("[dry-run]   First frame render: OK ({}×{})", fb->width(), fb->height());
            }
        } catch (const std::exception& e) {
            spdlog::error("[dry-run]   Render error: {}", e.what());
            return 1;
        }

        spdlog::info("[dry-run] ✅ Composition is valid — no rendering performed.");
        return 0;
    }

    // ── Graceful cancellation (SIGINT/SIGTERM) ──
    chronon3d::CancellationToken cancel_token;
    install_signal_cancellation(cancel_token);

    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) return 1;
    const auto& comp = *resolved.comp;

    RenderSettings settings = settings_from_args(args, !resolved.from_specscene);

    FfmpegExportOptions opts;
    opts.output = video_output;
    opts.frames_dir_name = args.frames_dir.empty() ? ("chronon_" + std::filesystem::path(args.comp_id).filename().string()) : args.frames_dir;
    opts.fps = args.fps;
    opts.crf = args.crf;
    opts.codec = args.codec;
    opts.hardware_encoder = args.hardware_encoder;
    opts.encode_preset = args.encode_preset;
    opts.tune = args.tune;
    opts.keep_frames = args.keep_frames;
    opts.chunks = args.chunks;
    opts.ffmpeg_mode = args.ffmpeg_mode;
    opts.ffmpeg_verbose = args.ffmpeg_verbose;
    opts.pipe_pixfmt = args.pipe_pixfmt;
    opts.color_output = args.color_output;
    opts.pipe_writer = args.pipe_writer;
    opts.encoder_backend = args.encoder_backend;
    if (opts.tune.empty() && opts.codec == "libx264") {
        opts.tune = "zerolatency";
        spdlog::info("[video] Auto-selecting x264 tune=zerolatency for low-latency pipe export");
    }
#if defined(__linux__)
    // If the user did not explicitly request a raw RGBA pipe, prefer YUV420P
    // for even-sized compositions. It cuts pipe bandwidth by 62.5% and routes
    // through the direct float→YUV path instead of the RGBA staging path.
    if (args.pipe_pixfmt == "rgba" &&
        comp.width() % 2 == 0 && comp.height() % 2 == 0 &&
        args.codec != "libx264rgb")
    {
        opts.pipe_pixfmt = "yuv420p";
        spdlog::info("[video] Auto-selecting yuv420p pipe pixel format for {}x{} output", comp.width(), comp.height());
    }
#endif
#ifdef __linux__
    if (opts.pipe_writer == "io_uring") {
        spdlog::warn("[video] io_uring pipe writer is experimental — output may be corrupted on some kernels; use --pipe-writer classic for stable exports");
    }
#endif

    opts.warmup_renderer = args.pipeline.warmup_renderer || (args.ffmpeg_mode == "pipe");
    opts.warmup_framebuffers = args.pipeline.warmup_framebuffers;
    opts.warmup_dummy_frame = args.pipeline.warmup_dummy_frame || (args.ffmpeg_mode == "pipe");

    // Pass cancellation token for graceful SIGINT handling
    opts.cancellation_token = &cancel_token;

    // ── CLI end is INCLUSIVE (user says --end 299 → renders frame 299) ────
    // The internal render loop uses exclusive end (current_frame < end).
    // Convert here so the user gets intuitive behavior.
    // When --end is not provided, fall back to comp.duration() which is
    // already the exclusive end (e.g. duration=300 means frames 0-299).
    // Use > to distinguish '--end provided by user' from 'default (0)'.
    // With >=, --start 0 without --end would render only 1 frame instead
    // of the full composition because args.end defaults to 0.
    const Frame end = (args.end > args.start) ? (args.end + 1) : comp.duration();
    
    return render_and_encode_ffmpeg(registry, comp, args.comp_id, settings, args.start, end, opts);
}

int command_video_camera(const CompositionRegistry& registry, const VideoCameraArgs& args) {
    auto axis = parse_motion_axis(args.axis);
    if (!axis) {
        spdlog::error("Unknown camera axis '{}'. Expected Tilt, Pan, or Roll.", args.axis);
        return 1;
    }

    std::string output = args.output.empty()
        ? chronon_artifact_path("videos", "camera_" + lower_copy(args.axis) + "_video.mp4").string()
        : args.output;

    RenderSettings settings = settings_from_args(args);

    animation::CameraMotionParams params;
    params.axis = *axis;
    if (*axis == animation::MotionAxis::Roll) {
        params.start_deg = args.roll_start_deg;
        params.end_deg = args.roll_end_deg;
    }
    // CLI end is inclusive; convert to exclusive for internal use
    // CLI end is inclusive; convert to exclusive for internal use.
    // comp isn't defined yet (created below via camera_motion_clip),
    // so use args.end as fallback for the no-`--end` case (same as original).
    const Frame cam_end = (args.end > args.start) ? (args.end + 1) : args.end;
    params.duration = cam_end - args.start;
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
    opts.tune = args.tune;
    opts.keep_frames = false; // default for camera motion
    opts.chunks = 1; // can't easily chunk here without extending args, default 1
    opts.ffmpeg_mode = "png";
    opts.ffmpeg_verbose = false;

    return render_and_encode_ffmpeg(registry, comp, comp.name(), settings, args.start, cam_end, opts);
}

} // namespace chronon3d::cli

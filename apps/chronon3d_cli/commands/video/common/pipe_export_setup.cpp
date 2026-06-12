#include "pipe_export_setup.hpp"
#include "pipe_export_helpers.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <spdlog/spdlog.h>

#include <chronon3d/core/profiling/profiling.hpp>

namespace chronon3d::cli {

std::unique_ptr<IVideoEncoder> create_pipe_encoder(
    const Composition& comp,
    const FfmpegExportOptions& opts,
    std::string& out_codec,
    FfmpegPipeOptions& out_pipe_options)
{
    const bool codec_auto = opts.codec == "auto";
    out_codec = codec_auto ? "libx264" : resolve_cli_ffmpeg_codec(opts.codec, opts.hardware_encoder);

    out_pipe_options = make_pipe_options(comp, opts, out_codec);

    auto encoder = create_video_encoder(opts);
    if (!encoder->open(out_pipe_options)) {
        spdlog::error("[video] Failed to open encoder");
        return nullptr;
    }

    return encoder;
}

std::unique_ptr<PipeExportSetupResult> setup_pipe_export(
    const CompositionRegistry& registry,
    const Composition& comp,
    const RenderSettings& settings,
    const FfmpegExportOptions& opts)
{
    auto result = std::make_unique<PipeExportSetupResult>();

    // ── Wall clock start ────────────────────────────────────────────────
    result->wall_t0 = profiling::now();
    result->setup_t0 = result->wall_t0;

    // ── Reset profiling globals ─────────────────────────────────────────
    profiling::g_live_framebuffer_bytes.store(0, std::memory_order_relaxed);
    profiling::g_peak_live_framebuffer_bytes.store(0, std::memory_order_relaxed);

    // ── Encoder ─────────────────────────────────────────────────────────
    {
        FfmpegPipeOptions pipe_options;
        result->encoder = create_pipe_encoder(comp, opts, result->codec, pipe_options);
        if (!result->encoder) {
            return nullptr;
        }
        track_pipe_encoder_process(opts, *result->encoder, result->sys_metrics);
    }

    // ── Renderer ────────────────────────────────────────────────────────
    {
        const auto renderer_t0 = profiling::now();
        result->renderer = create_renderer(registry, settings);
        const auto renderer_t1 = profiling::now();
        if (result->renderer) {
            if (auto* cnt = result->renderer->counters()) {
                const auto setup_ms = static_cast<uint64_t>(
                    profiling::duration_ms(renderer_t0, renderer_t1));
                cnt->setup_graph_parsing_ms.fetch_add(setup_ms, std::memory_order_relaxed);
            }
        }
    }

    // ── Arena + queue ───────────────────────────────────────────────────
    result->video_decoder = nullptr;
    result->arena_size = compute_pipe_arena_size(comp.width(), comp.height());
    result->triple_arena = std::make_unique<TripleBufferArena>(4, result->arena_size);

    return result;
}

} // namespace chronon3d::cli

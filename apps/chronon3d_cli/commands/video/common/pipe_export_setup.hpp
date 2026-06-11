#pragma once

#include "video_export_common.hpp"
#include "pipe_export_session.hpp"

#include <chronon3d/core/system_metrics.hpp>
#include <chronon3d/core/triple_buffer_arena.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/media/frame_source_provider.hpp>

// Forward declarations
namespace chronon3d { class SoftwareRenderer; }
#include <concurrentqueue/moodycamel/concurrentqueue.h>

#include <atomic>
#include <memory>
#include <string>

namespace chronon3d::cli {

/// Aggregated result of the pipe export setup phase.
struct PipeExportSetupResult {
    // Encoder
    std::unique_ptr<IVideoEncoder> encoder;
    std::string codec;                          // resolved codec name

    // Renderer
    std::shared_ptr<SoftwareRenderer> renderer;

    // Cache + arena + queue
    cache::NodeCache node_cache;
    media::MediaFrameProvider* video_decoder{nullptr};
    size_t arena_size{0};
    std::unique_ptr<TripleBufferArena> triple_arena;
    moodycamel::ConcurrentQueue<RenderFramePackage> queue;

    // Writer thread shared state
    std::atomic<bool> writer_failed{false};
    std::atomic<bool> writer_done{false};

    // System metrics collector (started, not yet sampled)
    chronon3d::SystemMetricsCollector sys_metrics;

    // Setup timestamps
    std::chrono::steady_clock::time_point wall_t0;
    std::chrono::steady_clock::time_point setup_t0;
};

/// Create encoder, resolve codec, build FfmpegPipeOptions, open encoder.
/// Returns nullptr on failure.
[[nodiscard]] std::unique_ptr<IVideoEncoder> create_pipe_encoder(
    const Composition& comp,
    const FfmpegExportOptions& opts,
    std::string& out_codec,
    FfmpegPipeOptions& out_pipe_options);

/// Run the full setup phase for pipe export.
/// Includes: encoder creation, renderer creation, arena allocation, queue init.
/// On failure returns nullopt (caller should check and abort).
[[nodiscard]] std::unique_ptr<PipeExportSetupResult> setup_pipe_export(
    const CompositionRegistry& registry,
    const Composition& comp,
    const RenderSettings& settings,
    const FfmpegExportOptions& opts);

} // namespace chronon3d::cli

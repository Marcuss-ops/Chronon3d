#pragma once

#include "pipe_export_session.hpp"
#include <chronon3d/media/video/video_job_execution_context.hpp>

#include <chronon3d/core/cpu_budget.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/backends/software/render_settings.hpp>

#include <memory>
#include <string>
#include <vector>

namespace chronon3d::cli {

// ── Pipeline phases (each extracted into its own .cpp file) ─────────────────

/// Phase 1: Create encoder, renderer, arena, queue, writer thread.
/// `video_runtimes` is the per-process VideoRuntimeRegistry (the daemon owns
/// one; CLI jobs create one lazily). When null a fresh registry is created
/// and owned by the session.
[[nodiscard]] std::unique_ptr<PipeExportSession> setup_pipe_export_session(
    const CompositionRegistry& registry,
    const CompiledComposition& compiled,
    const RenderSettings& settings,
    const FfmpegExportOptions& opts,
    Frame start,
    Frame end,
    const chronon3d::CpuBudget& cpu_budget,
    std::shared_ptr<media::VideoRuntimeRegistry> video_runtimes = nullptr,
    runtime::DeviceScheduler* device_scheduler = nullptr,
    std::shared_ptr<media::VideoJobExecutionContext> execution = nullptr,
    std::shared_ptr<SoftwareRenderer> warm_renderer = nullptr);

/// Phase 5: Pre-warm the framebuffer pool (separate from warmup_pipe_renderer).
void warmup_pipe_pool(PipeExportSession& session);

/// Phase 5b: Pre-allocate and pre-import execution slots for native NVENC.
void warmup_pipe_slots(PipeExportSession& session);

/// Phase 7: Close encoder, gather encoder metrics, log benchmark.
[[nodiscard]] EncoderCloseResult close_pipe_encoder(PipeExportSession& session);

/// Phase 5: Run the render loop and join the writer thread.
[[nodiscard]] RenderLoopOutput run_pipe_export_loop(
    PipeExportSession& session,
    const CompositionRegistry& registry,
    const CompiledComposition& compiled,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts);

/// Phase 10: Populate the result boundary model.
[[nodiscard]] PipeExportResult make_pipe_export_result(
    const PipeExportSession& session,
    const RenderLoopResult& loop_result,
    const EncoderCloseResult& close_result,
    double render_ms,
    double encode_ms,
    double wall_time_ms);

// ── Main orchestrator ───────────────────────────────────────────────────────

[[nodiscard]] PipeExportResult render_and_encode_native(
    const CompositionRegistry& registry,
    const CompiledComposition& compiled,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts,
    const chronon3d::CpuBudget& cpu_budget,
    std::shared_ptr<media::VideoRuntimeRegistry> video_runtimes = nullptr,
    runtime::DeviceScheduler* device_scheduler = nullptr,
    std::shared_ptr<media::VideoJobExecutionContext> execution = nullptr,
    std::shared_ptr<SoftwareRenderer> warm_renderer = nullptr);

} // namespace chronon3d::cli

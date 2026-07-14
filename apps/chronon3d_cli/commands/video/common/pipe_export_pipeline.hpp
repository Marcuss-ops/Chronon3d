#pragma once

#include "pipe_export_session.hpp"

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
[[nodiscard]] std::unique_ptr<PipeExportSession> setup_pipe_export_session(
    const CompositionRegistry& registry,
    const Composition& comp,
    const RenderSettings& settings,
    const FfmpegExportOptions& opts,
    Frame start,
    Frame end,
    const chronon3d::CpuBudget& cpu_budget);

/// Phase 5: Pre-warm the framebuffer pool (separate from warmup_pipe_renderer).
void warmup_pipe_pool(PipeExportSession& session);

/// Phase 7: Close encoder, gather encoder metrics, log benchmark.
[[nodiscard]] EncoderCloseResult close_pipe_encoder(PipeExportSession& session);

/// Phase 8-9: Collect and record telemetry.
void record_pipe_telemetry(
    const std::string& composition_id,
    PipeExportSession& session,
    const RenderLoopResult& loop_result,
    const EncoderCloseResult& close_result,
    const std::vector<chronon3d::telemetry::FrameTelemetryRecord>& telemetry_frames,
    double wall_time_ms,
    double render_ms,
    double encode_ms);

/// Phase 5: Run the render loop and join the writer thread.
[[nodiscard]] RenderLoopOutput run_pipe_export_loop(
    PipeExportSession& session,
    const CompositionRegistry& registry,
    const Composition& comp,
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

[[nodiscard]] PipeExportResult render_and_encode_ffmpeg_pipe(
    const CompositionRegistry& registry,
    const Composition& comp,
    const std::string& composition_id,
    const RenderSettings& settings,
    Frame start,
    Frame end,
    const FfmpegExportOptions& opts,
    const chronon3d::CpuBudget& cpu_budget);

} // namespace chronon3d::cli

#pragma once

#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>
#include "render_job.hpp"

#include <chrono>
#include <vector>

namespace chronon3d::cli {

/// Result of the legacy job loop.
///
/// Deliberately NOT named `RenderLoopResult`: that name belongs to the
/// pipe-export loop (commands/video/common/pipe_export_types.hpp) and carries a
/// completely different layout (PipeExportStatus + three doubles vs this POD).
/// Two same-named types in one namespace give their implicit constructors the
/// same mangled symbol, so the linker keeps a single definition and the other
/// TU constructs the wrong object — which is exactly how an uninitialized
/// `std::string` surfaced as `std::bad_alloc` in the encoder finalizer.
struct RenderJobLoopResult {
    bool ok{true};
    double total_render_ms{0.0};
    double total_encode_ms{0.0};
    int frames_written{0};

    std::vector<chronon3d::telemetry::FrameTelemetry> telemetry_frames;

    std::chrono::steady_clock::time_point loop_start;
    std::chrono::steady_clock::time_point loop_end;
};

RenderJobLoopResult run_render_job_loop(
    const RenderJob& job,
    SoftwareRenderer& renderer);

} // namespace chronon3d::cli

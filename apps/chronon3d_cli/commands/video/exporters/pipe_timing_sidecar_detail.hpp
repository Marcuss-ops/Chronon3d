#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// pipe_timing_sidecar_detail.hpp — shared internals for the split
// frame-timing sidecar builder TUs.
//
// write_frame_timing_sidecar() used to be a single ~700-line function.  It is
// now phased across sibling TUs (pure code move, no schema change):
//
//   - pipe_timing_sidecar_frames.cpp      sort + per-frame sections (phase 1)
//                                         + shared per-frame section builders
//   - pipe_timing_sidecar_summary.cpp     summary + job sections (phase 2)
//   - pipe_timing_sidecar_diagnostics.cpp cache/memory/wall/internal/startup
//                                         sections (phase 3)
//
// The public orchestrator in pipe_timing_sidecar.cpp threads a SidecarContext
// through the phases and writes the final file.
// ═══════════════════════════════════════════════════════════════════════════

#include "pipe_timing_sidecar.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace chronon3d::cli::pipe_timing_detail {

// Shared per-job state threaded through the sidecar builder phases.
struct SidecarContext {
    std::vector<chronon3d::telemetry::FrameTelemetry> frames;  // sorted by frame_number (phase 1)
    std::vector<chronon3d::telemetry::FrameTelemetry> enc;     // sorted by frame_number (phase 1)
    std::string video_path;
    double wall_time_ms = 0.0;
    double render_ms = 0.0;
    double encode_ms = 0.0;
    bool is_native = false;
};

// ── shared per-frame section builders (defined in the frames TU) ────────────

[[nodiscard]] double render_total_ms(const chronon3d::telemetry::FrameTelemetry& f);
[[nodiscard]] nlohmann::json build_render_section(const chronon3d::telemetry::FrameTelemetry& f);
[[nodiscard]] nlohmann::json build_conversion_section(
    bool is_native, const chronon3d::telemetry::FrameTelemetry* e);
[[nodiscard]] nlohmann::json build_encoder_section(
    bool is_native, const chronon3d::telemetry::FrameTelemetry* e);
[[nodiscard]] nlohmann::json build_image_section(const chronon3d::telemetry::FrameTelemetry& f);
[[nodiscard]] nlohmann::json build_text_section(const chronon3d::telemetry::FrameTelemetry& f);
[[nodiscard]] nlohmann::json build_cache_section(const chronon3d::telemetry::FrameTelemetry& f);
[[nodiscard]] const chronon3d::telemetry::FrameTelemetry* find_encoder_frame(
    const std::vector<chronon3d::telemetry::FrameTelemetry>& enc, int frame_number);

// ── phase 1: document skeleton + sorted per-frame sections ──────────────────
// Sorts ctx.frames / ctx.enc by frame_number, fills the document skeleton and
// the per-frame "frame_times_ms" array.  Returns the per-frame durations.
std::vector<double> build_frame_times_section(nlohmann::json& out, SidecarContext& ctx);

// ── phase 2: summary + job sections ─────────────────────────────────────────
void build_summary_and_job_sections(
    nlohmann::json& out, const SidecarContext& ctx,
    const std::vector<double>& durations, const pipe_timing::JobTimings& timings);

// ── phase 3: cache / memory / exclusive-wall / internal-profiling / startup ─
void build_diagnostics_sections(
    nlohmann::json& out, const pipe_timing::JobTimings& timings, std::size_t frame_count);

} // namespace chronon3d::cli::pipe_timing_detail

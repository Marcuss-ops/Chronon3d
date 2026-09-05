// ═══════════════════════════════════════════════════════════════════════════
// timeline/render_job.hpp — canonical unified render request and job values.
//
// RenderRequest is unresolved authoring/CLI input. RenderJob is the single
// resolved value passed to execution for still, sequence, and video modes.
// Neither owns backend/runtime/cache state; the executor creates those details.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/cpu_budget.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>
#include <chronon3d/media/video/video_job_execution_context.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d {

enum class RenderMode : std::uint8_t {
    Still    = 0,
    Sequence = 1,
    Video    = 2,
};

/// Video-specific plain settings carried through the canonical request/job.
struct VideoSettings {
    int         fps{30};
    // Canonical media clock. `fps` remains only as a CLI compatibility alias
    // until command parsing is fully rational; execution must use these.
    int         fps_num{30};
    int         fps_den{1};
    std::string rate_control_mode{"crf"};
    int         crf{16};
    int         qp{-1};
    std::int64_t bitrate{0};
    std::string codec{"auto"};
    std::string encode_preset{"slow"};
    std::string tune;

    // ── Encoder-option explicitness ────────────────────────────────────────
    // True only when the value above was explicitly requested by the user, a
    // render-plan file, or an IPC payload. Engine placeholder defaults stay
    // false so the encoder-configuration resolver can distinguish USER INTENT
    // from CHRONON DEFAULTS (e.g. an NVENC job that never mentioned rate
    // control must not be treated as an explicit CRF request). Set by the
    // CLI/plan/daemon boundaries before the job is resolved.
    bool rate_control_mode_explicit{false};
    bool crf_explicit{false};
    bool qp_explicit{false};
    bool bitrate_explicit{false};
    bool encode_preset_explicit{false};
    bool tune_explicit{false};

    bool        keep_frames{false};
    std::string frames_dir;
    int         chunks{1};
    // Optional compressed source used by the GOP smart-copy planner.
    // Empty keeps the regular render path.
    std::string gop_source;
    bool        gop_copy_only{false};

    std::string hardware_encoder{"none"};
    std::string ffmpeg_mode{"pipe"};
    bool        ffmpeg_verbose{false};
    std::string pipe_pixfmt{"rgba"};
    std::string color_output{"srgb"};
    std::string pipe_writer{"classic"};
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    std::string encoder_backend{"native"};
#else
    std::string encoder_backend{"pipe"};
#endif
    std::string sink_type{"ffmpeg"};
    bool        dry_run{false};
};

struct RenderExecutionOptions {
    std::string log_level{"info"};
    bool benchmark_all{false};
    bool report{false};
    bool diagnostic_plan{false};
    std::string command_line;

    bool        warmup_renderer{false};
    std::size_t warmup_framebuffers{2};
    bool        warmup_dummy_frame{false};

    CpuBudget cpu_budget{};
    std::optional<Config> config;
    // Runtime-owned asset mount; never stored in CompositionSpec.
    std::optional<std::filesystem::path> assets_root;
    // Timeline tracing (--trace): output path for the .pftrace; empty = off.
    // The file is written once at job end — TraceSession drains the in-memory
    // RING_BUFFER only in finish(), never during the render hot path.
    std::filesystem::path trace_output;
    // Trace capture level: pipeline | nodes | full (see trace::TraceLevel).
    std::string trace_level{"pipeline"};
};

struct RenderDiagnostics {
    std::uint32_t version{0};
};

enum class RenderJobErrorCode : std::uint8_t {
    InvalidJob = 0,
    UnsupportedMode,
    SetupFailed,
    ValidationFailed,
    RenderFailed,
};

struct RenderJobError {
    RenderJobErrorCode code{RenderJobErrorCode::RenderFailed};
    std::string message;
};

struct RenderJobOutput {
    RenderMode mode{RenderMode::Still};
    std::string output;
    int frames_written{0};
};

/// Unresolved input to the canonical resolve → execute pipeline.
/// It carries logical composition input but no Composition instance, registry
/// pointer, renderer, resolver, cache, or runtime state.
struct RenderRequest {
    std::string comp_id;
    /// Optional canonical compiled plan. When present, resolution does not
    /// re-enter the composition registry or construct a second composition.
    std::shared_ptr<const CompiledComposition> compiled_composition;
    CompositionInput input;
    RenderMode mode{RenderMode::Still};
    Frame still_frame{0};
    Frame first_frame{0};
    Frame last_frame{0};
    Frame frame_step{1};

    std::string output;
    RenderSettings settings;
    VideoSettings video_settings;
    RenderExecutionOptions execution;
    RenderDiagnostics diagnostics{};
};

/// Single resolved execution value covering still, sequence, and video.
///
/// `registry` is a non-owning execution dependency pinned by the CLI/host.
/// Execution owns only the immutable compiled composition. Metadata resolved
/// from its definition is retained on the same value; no direct Composition
/// execution path is present.
struct RenderJob {
    const CompositionRegistry* registry{nullptr};
    std::string comp_id;
    std::shared_ptr<const CompiledComposition> compiled;
    CompositionMetadata metadata{};

    RenderMode mode{RenderMode::Still};
    Frame still_frame{0};
    Frame first_frame{0};
    Frame last_frame{0};
    Frame frame_step{1};

    // Optional non-contiguous frame selection for preview/contact-sheet jobs.
    // The canonical executor renders these frames in order with one session.
    std::vector<Frame> selected_frames;

    std::string output;
    RenderSettings settings;
    VideoSettings video_settings;
    RenderExecutionOptions execution;
    RenderDiagnostics diagnostics{};

    static RenderJob still(std::string id,
                           std::shared_ptr<const CompiledComposition> c,
                           Frame frame,
                           std::string out) {
        RenderJob job;
        job.comp_id = std::move(id);
        job.compiled = std::move(c);
        job.mode = RenderMode::Still;
        job.still_frame = frame;
        job.output = std::move(out);
        return job;
    }

    static RenderJob sequence(std::string id,
                              std::shared_ptr<const CompiledComposition> c,
                              Frame first,
                              Frame last,
                              std::string out) {
        RenderJob job;
        job.comp_id = std::move(id);
        job.compiled = std::move(c);
        job.mode = RenderMode::Sequence;
        job.first_frame = first;
        job.last_frame = last;
        job.output = std::move(out);
        return job;
    }

    static RenderJob video_job(std::string id,
                               std::shared_ptr<const CompiledComposition> c,
                               Frame first,
                               Frame last,
                               std::string out) {
        RenderJob job;
        job.comp_id = std::move(id);
        job.compiled = std::move(c);
        job.mode = RenderMode::Video;
        job.first_frame = first;
        job.last_frame = last;
        job.output = std::move(out);
        return job;
    }

    [[nodiscard]] Frame frame_count() const noexcept {
        if (!selected_frames.empty()) {
            return Frame{static_cast<std::int64_t>(selected_frames.size())};
        }
        if (last_frame <= first_frame) return Frame{0};
        return last_frame - first_frame + Frame{1};
    }

    /// Composition-bound probe retained for factory-created jobs. The executor
    /// separately validates the non-owning registry dependency before running.
    [[nodiscard]] explicit operator bool() const noexcept {
        return compiled != nullptr && compiled->composition != nullptr;
    }
};

} // namespace chronon3d

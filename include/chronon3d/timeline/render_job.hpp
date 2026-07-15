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

#include <cstddef>
#include <cstdint>
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
    int         crf{16};
    std::string codec{"auto"};
    std::string encode_preset{"slow"};
    std::string tune;
    bool        keep_frames{false};
    std::string frames_dir;
    int         chunks{1};

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
/// Composition ownership stays explicit through `comp`. Metadata resolved from
/// the descriptor is retained on the same value; no parallel plan/job type is
/// introduced.
struct RenderJob {
    const CompositionRegistry* registry{nullptr};
    std::string comp_id;
    std::shared_ptr<const Composition> comp;
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
                           std::shared_ptr<const Composition> c,
                           Frame frame,
                           std::string out) {
        RenderJob job;
        job.comp_id = std::move(id);
        job.comp = std::move(c);
        job.mode = RenderMode::Still;
        job.still_frame = frame;
        job.output = std::move(out);
        return job;
    }

    static RenderJob sequence(std::string id,
                              std::shared_ptr<const Composition> c,
                              Frame first,
                              Frame last,
                              std::string out) {
        RenderJob job;
        job.comp_id = std::move(id);
        job.comp = std::move(c);
        job.mode = RenderMode::Sequence;
        job.first_frame = first;
        job.last_frame = last;
        job.output = std::move(out);
        return job;
    }

    static RenderJob video_job(std::string id,
                               std::shared_ptr<const Composition> c,
                               Frame first,
                               Frame last,
                               std::string out) {
        RenderJob job;
        job.comp_id = std::move(id);
        job.comp = std::move(c);
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

    [[nodiscard]] explicit operator bool() const noexcept {
        return registry != nullptr && comp != nullptr;
    }
};

} // namespace chronon3d

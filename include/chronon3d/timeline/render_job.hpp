// ═══════════════════════════════════════════════════════════════════════════
// timeline/render_job.hpp — canonical unified render job descriptor.
//
// RenderJob is the single value passed from CLI planning to execution for
// still, sequence, and video modes.  It owns no backend/runtime/cache state;
// those remain implementation details created by the executor.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/cpu_budget.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d {

class CompositionRegistry;

enum class RenderMode : std::uint8_t {
    Still    = 0,
    Sequence = 1,
    Video    = 2,
};

/// Video-specific values carried by the canonical job.  These are plain
/// execution settings, not an encoder or resolver abstraction.
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

/// Cross-mode execution controls previously stored in CLI-only job plans.
/// Keeping them on RenderJob removes duplicated plan types while preserving a
/// copyable, inspectable job value.
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

/// Single job descriptor covering still, sequence, and video rendering.
///
/// `registry` is a non-owning execution dependency pinned by the CLI/host.
/// Composition ownership stays explicit through `comp`.
struct RenderJob {
    const CompositionRegistry*             registry{nullptr};
    std::string                            comp_id;
    std::shared_ptr<const Composition>     comp;

    RenderMode mode{RenderMode::Still};
    Frame      still_frame{0};
    Frame      first_frame{0};
    Frame      last_frame{0};
    Frame      frame_step{1};

    // Optional non-contiguous frame selection for preview/contact-sheet jobs.
    // When populated, the canonical executor renders these frames in order
    // with one renderer/session and ignores the contiguous range loop. This is
    // execution data on the existing RenderJob, not a preview-specific plan.
    std::vector<Frame> selected_frames;

    std::string output;

    RenderSettings         settings;
    VideoSettings          video_settings;
    RenderExecutionOptions execution;
    RenderDiagnostics      diagnostics{};

    static RenderJob still(std::string id,
                           std::shared_ptr<const Composition> c,
                           Frame frame,
                           std::string out) {
        RenderJob job;
        job.comp_id     = std::move(id);
        job.comp        = std::move(c);
        job.mode        = RenderMode::Still;
        job.still_frame = frame;
        job.output      = std::move(out);
        return job;
    }

    static RenderJob sequence(std::string id,
                              std::shared_ptr<const Composition> c,
                              Frame first,
                              Frame last,
                              std::string out) {
        RenderJob job;
        job.comp_id     = std::move(id);
        job.comp        = std::move(c);
        job.mode        = RenderMode::Sequence;
        job.first_frame = first;
        job.last_frame  = last;
        job.output      = std::move(out);
        return job;
    }

    static RenderJob video_job(std::string id,
                               std::shared_ptr<const Composition> c,
                               Frame first,
                               Frame last,
                               std::string out) {
        RenderJob job;
        job.comp_id     = std::move(id);
        job.comp        = std::move(c);
        job.mode        = RenderMode::Video;
        job.first_frame = first;
        job.last_frame  = last;
        job.output      = std::move(out);
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
        return comp != nullptr;
    }
};

} // namespace chronon3d

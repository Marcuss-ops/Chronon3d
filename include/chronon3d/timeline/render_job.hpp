// ═══════════════════════════════════════════════════════════════════════════
// timeline/render_job.hpp — D1: unified render job descriptor.
//
// Replaces the pre-D1 split (cli::RenderJobPlan for render/still +
// cli::VideoJobPlan for video + separate command paths) with a single
// canonical RenderJob type covering all three render modes.
//
//   RenderMode::Still    — single frame to PNG
//   RenderMode::Sequence — frame range to image sequence
//   RenderMode::Video    — frame range encoded to video
//
// The CLI converts its args (RenderArgs / StillArgs / VideoArgs) into
// a RenderJob and calls a single executor — no second orchestration.
//
// Per-frame execution state (RenderSession, CameraSession) is kept
// separate from the job descriptor to preserve copy semantics.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace chronon3d {

// ── RenderMode ─────────────────────────────────────────────────────────

enum class RenderMode : std::uint8_t {
    Still    = 0,  // single frame → image file
    Sequence = 1,  // frame range → image sequence
    Video    = 2,  // frame range → video encode
};

// ── VideoSettings ──────────────────────────────────────────────────────

/// Video-specific parameters folded into the unified RenderJob.
/// System-local config (ffmpeg path, pipe mode) lives outside the job.
struct VideoSettings {
    int         fps{30};
    int         crf{16};
    std::string codec{"auto"};
    std::string encode_preset{"slow"};
    std::string tune;
    bool        keep_frames{false};
    std::string frames_dir;
    int         chunks{1};
};

// ── RenderDiagnostics (V2 placeholder, unchanged from P3-C) ────────────

struct RenderDiagnostics {
    std::uint32_t version{0};
};

// ── RenderJob — canonical unified render descriptor ───────────────────

/// Single job descriptor covering still, sequence, and video render.
///
/// Copyable value type.  Per-frame execution payload (RenderSession,
/// CameraSession) is assembled by the executor, NOT stored here.
///
/// Factory conveniences:
///   RenderJob::still("hero", Frame{42}, "hero.png")
///   RenderJob::sequence("intro", Frame{0}, Frame{90}, "frame_%04d.png")
///   RenderJob::video_job("intro", Frame{0}, Frame{90}, "intro.mp4")
struct RenderJob {
    // ── Identity ────────────────────────────────────────────────────

    std::string                       comp_id;
    std::shared_ptr<const Composition> comp;        // resolved from comp_id

    // ── Mode + frames ───────────────────────────────────────────────

    RenderMode mode{RenderMode::Still};
    Frame      still_frame{0};            // Still mode target frame
    Frame      first_frame{0};            // Sequence / Video start (inclusive)
    Frame      last_frame{0};             // Sequence / Video end (inclusive)

    // ── Output ──────────────────────────────────────────────────────

    std::string output;                    // file path or printf pattern

    // ── Settings ────────────────────────────────────────────────────

    RenderSettings settings;
    VideoSettings  video_settings;

    // ── Diagnostics ─────────────────────────────────────────────────

    RenderDiagnostics diagnostics{};

    // ── Factory conveniences ────────────────────────────────────────

    /// Create a still-frame render job.
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

    /// Create an image-sequence render job.
    static RenderJob sequence(std::string id,
                              std::shared_ptr<const Composition> c,
                              Frame first,
                              Frame last,
                              std::string out) {
        RenderJob job;
        job.comp_id    = std::move(id);
        job.comp       = std::move(c);
        job.mode       = RenderMode::Sequence;
        job.first_frame = first;
        job.last_frame  = last;
        job.output     = std::move(out);
        return job;
    }

    /// Create a video render job.
    static RenderJob video_job(std::string id,
                               std::shared_ptr<const Composition> c,
                               Frame first,
                               Frame last,
                               std::string out) {
        RenderJob job;
        job.comp_id    = std::move(id);
        job.comp       = std::move(c);
        job.mode       = RenderMode::Video;
        job.first_frame = first;
        job.last_frame  = last;
        job.output     = std::move(out);
        return job;
    }

    /// Frame count (valid for Sequence and Video modes).
    [[nodiscard]] Frame frame_count() const noexcept {
        if (last_frame <= first_frame) return Frame{0};
        return last_frame - first_frame + Frame{1};
    }

    /// True if the composition has been resolved.
    [[nodiscard]] explicit operator bool() const noexcept {
        return comp != nullptr;
    }
};

} // namespace chronon3d

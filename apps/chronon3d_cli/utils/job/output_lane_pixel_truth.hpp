#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// apps/chronon3d_cli/utils/job/output_lane_pixel_truth.hpp
//
// TICKET-TEXT-QUALITY-V2 (P0-0b) — the image output lane (still / PNG
// sequence) is NOT a pixel-truth surface.
//
// Why this header exists. `render -o x.png` renders through the image writer,
// which produces a frame that does not compose the text layers and does not
// vary with the frame index or the backend (profile in
// docs/evidence/image-path-defect-2026-09-16.md). The renderer itself is not
// what is broken here: the image lane is simply not a measurement of what the
// renderer composed, and until this notice existed nothing in the CLI said so.
// The pre-write sanity scan cannot say it either — its contract is NaN/Inf
// only, so a uniform, duplicated, text-free frame is accepted *by
// construction* (apps/chronon3d_cli/utils/job/render_job_write_frame_sanity.hpp).
//
// The canonical pre-encode truth is the raw video sink (`--video-sink raw`):
// host RGBA frames written with no encoder and no container, so what is
// measured is the renderer and not a converter. The other valid truth surface
// is the decoded frame of the encoded output.
//
// ONE authority. Both lane-decision sites — the direct `render` route
// (utils/job/render_job.cpp) and the `render --plan` route
// (commands/render/command_render_plan_internal.cpp) — ask this header whether
// the lane they just selected is a truth surface, and print the same notice.
// Two independently written messages would drift, and a lane added later has to
// answer the question here instead of quietly becoming a third opinion.
// ─────────────────────────────────────────────────────────────────────────────

#include <chronon3d/timeline/render_job.hpp>

#include <spdlog/spdlog.h>

#include <string_view>

namespace chronon3d::cli::pixel_truth {

/// The canonical notice. It names both the replacement surface and the ticket,
/// so a reader who finds it in a log does not have to search for why their
/// still does not contain the text they authored.
inline constexpr std::string_view kImageLaneNotice =
    "pixel truth: the image output lane (still / PNG sequence) is not a "
    "pixel-truth surface — it does not compose the text layers and does not "
    "vary per frame or per backend (TICKET-TEXT-QUALITY-V2 P0-0b). For "
    "pre-encode truth use `--video-sink raw`, or measure the decoded frame of "
    "the encoded output.";

/// True only for the lane whose frames are the renderer's own output.
[[nodiscard]] inline constexpr bool lane_is_pixel_truth_surface(
    RenderMode mode) noexcept {
    return mode == RenderMode::Video;
}

/// The notice for a selected lane: empty exactly when the lane IS a truth
/// surface. Pure, so a unit test can pin both the rule and the text without
/// capturing a log stream.
[[nodiscard]] inline constexpr std::string_view lane_notice(
    RenderMode mode) noexcept {
    return lane_is_pixel_truth_surface(mode) ? std::string_view{}
                                             : kImageLaneNotice;
}

/// Announce the selected lane at the decision point. Called once per job by
/// each route, so the warning appears once in a render log and never
/// per frame.
inline void announce_lane(RenderMode mode) {
    const auto notice = lane_notice(mode);
    if (!notice.empty()) {
        spdlog::warn("{}", notice);
    }
}

} // namespace chronon3d::cli::pixel_truth

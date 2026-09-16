// =============================================================================
// copy_lane_coverage.hpp — the copy lane's TRUST BOUNDARY over a render plan.
//
// The GOP copy lane (`execute_direct_mux_gop_plan`, gop_smart_copy) publishes
// the compressed source's packets VERBATIM for every interval the caller did
// not declare as changed, and renders/encodes only the declared ones
// (`media::ChangedInterval`). That makes the declaration load-bearing, and
// until now nothing checked it against the plan:
//
//   * `changed_intervals` is written by the producer and trusted as-is, so a
//     localization job that under-declares its overlay's active window gets the
//     base packets spliced into the published file and the overlay simply
//     absent from those frames — exit 0, no warning;
//   * `--gop-copy-only` declares "the source already IS the output" while the
//     plan may still carry text/image layers, which no packet copy can contain
//     — again exit 0, again unlocalized frames.
//
// Both are the same failure: the pixels the plan declares and the pixels the
// lane publishes disagree, and the job reports success. This module is the one
// authority that refuses that disagreement, called once where the decoded plan
// and the normalized job first meet (`execute_render_plan`).
//
// What it does NOT check (declared, so the boundary stays honest): that the
// copied intervals are the *right* ones, that the base video layer really is
// the compressed source, or anything about `trim` intervals. It only refuses a
// plan whose rendered content is not representable by what the caller declared.
// =============================================================================

#pragma once

#include <chronon3d/media/video/video_execution_resolver.hpp>  // media::ChangedInterval
#include <chronon3d/render_plan/render_plan.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace chronon3d::cli {

/// Half-open source-time window (seconds) in which one layer draws.
struct CopyLaneLayerWindow {
    std::string layer_id;
    double first_second{0.0};
    double last_second{0.0};
};

struct CopyLaneCoverageVerdict {
    bool ok{true};
    /// Fail-closed reason, empty when `ok`. Always names the layer, the window
    /// and the remedy: this refusal is read by a producer, not by a debugger.
    std::string message;
    /// The layer windows the caller's declaration does not cover.
    std::vector<CopyLaneLayerWindow> uncovered;
};

/// A layer is ADDITIVE when it draws content the copied base does not already
/// have. Every non-video layer is additive by construction. A video layer is
/// the copied base itself in the passthrough case, so the FIRST video layer is
/// not additive on its own; a second video layer (picture-in-picture, composited
/// source) is. Layers that cannot draw — zero opacity, or a text layer with no
/// text — are not additive.
[[nodiscard]] bool layer_is_additive(const render_plan::RenderPlan& plan,
                                     std::size_t index) noexcept;

/// Refuse the two silent-wrong-output cases above.
///
/// `gop_copy_only` refuses when the plan draws anything additive, regardless of
/// declarations. Otherwise, when `changed_intervals` is non-empty, every
/// additive layer's active window must be inside the union of the `content`
/// intervals; an empty interval list keeps the legacy whole-export
/// content-change semantics (everything is re-encoded, nothing is copied) and
/// therefore needs no coverage.
[[nodiscard]] CopyLaneCoverageVerdict validate_copy_lane_coverage(
    const render_plan::RenderPlan& plan,
    bool gop_copy_only,
    std::span<const media::ChangedInterval> changed_intervals);

} // namespace chronon3d::cli

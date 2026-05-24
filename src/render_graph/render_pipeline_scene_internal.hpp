#pragma once

#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include "builder/graph_builder_internal.hpp"
#include <unordered_map>
#include <optional>

namespace chronon3d::graph::detail {

// ── Camera change detection ──────────────────────────────────────────────────
// Compares the current camera with the previously stored state.
[[nodiscard]] inline bool camera_changed(
    const Camera2_5D& current,
    const Camera2_5D* prev,
    bool prev_valid
) {
    if (!prev_valid) return true;
    if (prev->enabled != current.enabled) return true;
    if (!current.enabled) return false;
    return prev->position      != current.position  ||
           prev->zoom          != current.zoom      ||
           prev->fov_deg       != current.fov_deg   ||
           prev->rotation      != current.rotation  ||
           prev->projection_mode != current.projection_mode;
}

// ── Dirty rect computation ───────────────────────────────────────────────────
// Computes per-layer bounding boxes (TBB parallel), diffs them against the
// previous frame's bboxes, and optionally applies the scroll optimisation.
//
// Returns the union dirty rectangle (nullopt = full frame).
struct DirtyRectOutput {
    std::unordered_map<std::string, SoftwareRenderer::LayerBBoxState> layer_bboxes;
    std::optional<raster::BBox> dirty_rect;
    bool use_dirty_rects{false};
};

DirtyRectOutput compute_dirty_rect(
    const RenderGraphContext& ctx,
    const LayerResolutionResult& resolved,
    const RenderSettings& settings,
    SoftwareRenderer* sw_renderer,
    Frame frame,
    i32 width,
    i32 height
);

} // namespace chronon3d::graph::detail

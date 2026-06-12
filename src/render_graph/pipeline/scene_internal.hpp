#pragma once

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/backends/software/renderer_types.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/core/tile_grid.hpp>
#include <chronon3d/core/dirty_tile_mask.hpp>
#include "camera_change_policy.hpp"
#include "dirty_safety_policy.hpp"
#include "../builder/graph_builder_internal.hpp"
#include <unordered_map>
#include <optional>

namespace chronon3d {
class SoftwareRenderer;
}

namespace chronon3d::graph::detail {

// camera_changed() is in camera_change_policy.hpp
// is_safe_for_dirty_rects() and has_layer_with_spatial_effects() are in dirty_safety_policy.hpp

// ── Dirty rect computation ───────────────────────────────────────────────────
// Computes per-layer bounding boxes (TBB parallel), diffs them against the
// previous frame's bboxes, and optionally applies the scroll optimisation.
//
// Returns the union dirty rectangle (nullopt = full frame).
struct DirtyRectOutput {
    std::unordered_map<std::string, LayerBBoxState> layer_bboxes;
    std::optional<raster::BBox> dirty_rect;
    bool use_dirty_rects{false};

    // Tile-based dirty tracking (V2): populated when settings.tile_size > 0
    // and settings.enable_dirty_bitmask are both active.
    // Falls back to dirty_rect when tile-based tracking is not available.
    std::optional<raster::TileGrid> tile_grid;
    std::optional<raster::DirtyTileMask> dirty_tiles;
    bool use_dirty_tiles{false};
};

DirtyRectOutput compute_dirty_rect(
    const RenderGraphContext& ctx,
    const LayerResolutionResult& resolved,
    const Scene& scene,
    const RenderSettings& settings,
    ::chronon3d::SoftwareRenderer* sw_renderer,
    Frame frame,
    i32 width,
    i32 height
);

} // namespace chronon3d::graph::detail

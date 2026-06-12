#pragma once

// ---------------------------------------------------------------------------
// renderer_types.hpp
//
/// @file    renderer_types.hpp
/// @brief   Lightweight type definitions extracted from SoftwareRenderer.
///
/// These types were originally nested inside SoftwareRenderer.  Extracting
/// them to their own header allows pipeline files to use `LayerBBoxState`,
/// `RendererFrameHistory`, etc. without including the full
/// `software_renderer.hpp` (which brings in ~40 transitive headers).
///
/// SoftwareRenderer still composes from these types via inclusion — the
/// public accessor methods are unchanged.
// ---------------------------------------------------------------------------

#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace chronon3d {

/// Per-layer bounding box + diff state for dirty-rect tracking.
///
/// Each active resolved layer produces one LayerBBoxState per frame.
/// The dirty-rect system diffs current vs. previous states to compute
/// the union of regions that need re-rendering.
struct LayerBBoxState {
    raster::BBox bbox;
    Mat4 world_matrix;
    f32 opacity{1.0f};
    bool visible{true};
    bool cache_static{false};
    bool uses_2_5d_projection{false};
    uint64_t content_hash{0};
};

/// Per-frame camera + fingerprint history.
///
/// Carried forward from the previous frame's render_scene_via_graph() call
/// for use by fast-path reuse checks and dirty-rect diffing.
struct RendererFrameHistory {
    Frame prev_frame{-1};
    Camera2_5D prev_camera;
    bool prev_camera_valid{false};
    uint64_t prev_scene_fingerprint{0};
    uint64_t prev_static_scene_fingerprint{0};
    uint64_t prev_graph_structure_fingerprint{0};
    uint64_t prev_active_at_fingerprint{0};
};

/// Telemetry counters for dirty-rect / tile-execution decisions.
///
/// Populated by render_scene_via_graph() after each frame and queried
/// by the CLI (e.g. dry-run, telemetry report).
struct RendererDirtyTelemetry {
    double last_dirty_area_ratio{1.0};
    int last_layer_count{0};
    bool last_dirty_rect_enabled{false};
    std::optional<raster::BBox> last_dirty_rect;
    bool last_tile_execution_used{false};
    bool last_fast_path_reused{false};
    bool last_graph_reused{false};
};

/// Per-layer bbox history for dirty-rect frame-to-frame diffing.
///
/// Stores the previous frame's bbox state for every active layer so
/// the dirty-rect system can detect added, removed, moved, and
/// content-changed layers.
struct RendererLayerHistory {
    std::unordered_map<std::string, LayerBBoxState> prev_layer_bboxes;
};

} // namespace chronon3d

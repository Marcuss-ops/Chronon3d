#pragma once

// ---------------------------------------------------------------------------
// runtime/dirty_history.hpp
//
// Per-frame dirty-rect / tiling / reuse telemetry — canonical definition.
//
// WP-3 PR 3.2 — this header is now the canonical home for BOTH:
//   - `DirtyHistory`:              the per-frame dirty-rect / tile / reuse
//                                  telemetry counters + folded-layer bbox
//                                  state (the previous_layers field below).
//   - `LayerBBoxState`:            the per-layer bbox + diff state captured
//                                  per frame; was canonically defined in
//                                  `<chronon3d/math/renderer_state.hpp>`.
//
// Migration rationale:
//   - Folded into DirtyHistory (WP-3 PR 3.2): `RendererLayerHistory` is gone.
//     Its sole payload `prev_layer_bboxes` is now `DirtyHistory::previous_layers`.
//   - Moved here (WP-3 PR 3.2): `LayerBBoxState` previously lived in
//     `<chronon3d/math/renderer_state.hpp>`; the canonical home is now this
//     header so that `LayerBBoxState` is reachable wherever `DirtyHistory`
//     is consumed (the dependency direction `dirty_history.hpp → math/`
//     already pulled in `raster::BBox` so adding `LayerBBoxState` here
//     avoids the would-be circular include that `dirty_history.hpp`
//     adopting `previous_layers<LayerBBoxState, std::string>` would have
//     needed).
//
// Backward-compatibility aliases (`RendererFrameHistory` /
// `RendererDirtyTelemetry`) were REMOVED in WP-3 PR 3.2 (no external SDK
// consumer holds them; internal code is fully updated).
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>

#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/render_graph/pipeline/execution_decision.hpp>

namespace chronon3d {

/// Content dimensions captured by the dirty-history producer.  These are
/// presence bits, not change bits: FrameDeltaCompiler compares the matching
/// fingerprints between frames and turns differences into LayerDeltaChange
/// flags.  Keeping them on the history record makes the semantic diff
/// available without teaching the compiler about scene/model internals.
enum LayerSemanticKind : std::uint32_t {
    SemanticText = 1u << 0,
    SemanticColor = 1u << 1,
    SemanticImage = 1u << 2,
    SemanticEffects = 1u << 3,
    SemanticVideoSource = 1u << 4,
};

/// Per-layer bounding box + diff state for dirty-rect tracking.
///
/// Each active resolved layer produces one `LayerBBoxState` per frame.
/// The dirty-rect system diffs current vs. `DirtyHistory::previous_layers`
/// to compute the union of regions that need re-rendering.
///
/// WP-3 PR 3.2 — public fields are deliberately writable so tests can
/// seed non-default content for the strict-preservation assertions on
/// `previous_layers` (previously blocked by the "sealed" status of the
/// `RendererLayerHistory` wrapper that held this map).
struct LayerBBoxState {
    raster::BBox bbox;
    Mat4 world_matrix;
    f32 opacity{1.0f};
    bool visible{true};
    bool cache_static{false};
    bool uses_2_5d_projection{false};
    uint64_t content_hash{0};

    // Optional semantic fingerprints.  They are additive so older producers
    // that only fill content_hash retain the original coarse content diff.
    std::uint64_t structure_hash{0};
    std::uint64_t text_hash{0};
    std::uint64_t color_hash{0};
    std::uint64_t image_hash{0};
    std::uint64_t effects_hash{0};
    std::uint64_t video_source_hash{0};
    std::uint32_t semantic_presence{0};
    bool semantic_fingerprints_valid{false};
};

/// Per-frame dirty-rect / tiling / reuse telemetry.
///
/// Populated by `render_scene_via_graph()` after each frame and queried
/// by the CLI (e.g. dry-run, telemetry report).
///
/// WP-3 PR 3.2 — `previous_layers` (formerly
/// `RendererLayerHistory::prev_layer_bboxes`) is now a member of this
/// struct so the per-frame "previous state" lives in ONE owner per
/// frame, not across two.  `reset_job()` reaches both the telemetry
/// counters and `previous_layers` together; `reset_frame_temporaries()`
/// preserves `previous_layers` so intermediate frame deltas survive
/// across per-frame boundaries.
struct DirtyHistory {
    // ── Per-frame dirty / tile / reuse telemetry counters (Phase 2) ──────
    double last_dirty_area_ratio{1.0};
    int last_layer_count{0};
    bool last_dirty_rect_enabled{false};
    std::optional<raster::BBox> last_dirty_rect;
    bool last_tile_execution_used{false};
    bool last_fast_path_reused{false};
    bool last_graph_reused{false};
    graph::FrameExecutionPath last_execution_path{graph::FrameExecutionPath::FullRgb};

    // Empirical execution-cost model.  The first samples are deliberately
    // observational: the policy needs at least two samples for each path
    // before it is allowed to reject tiled execution.  This avoids replacing
    // a measured decision with a resolution- or scene-specific magic number.
    double full_frame_exec_ms_ewma{0.0};
    double tile_exec_ms_ewma{0.0};
    std::uint32_t full_frame_cost_samples{0};
    std::uint32_t tile_cost_samples{0};

    void record_execution_cost(bool tile_path, double elapsed_ms) noexcept {
        if (!std::isfinite(elapsed_ms) || elapsed_ms < 0.0) return;
        constexpr double kAlpha = 0.25;
        double& ewma = tile_path ? tile_exec_ms_ewma : full_frame_exec_ms_ewma;
        auto& samples = tile_path ? tile_cost_samples : full_frame_cost_samples;
        ewma = samples == 0 ? elapsed_ms : (kAlpha * elapsed_ms + (1.0 - kAlpha) * ewma);
        ++samples;
    }

    [[nodiscard]] bool tile_cost_model_ready() const noexcept {
        return full_frame_cost_samples >= 2 && tile_cost_samples >= 2 &&
               std::isfinite(full_frame_exec_ms_ewma) &&
               std::isfinite(tile_exec_ms_ewma);
    }

    // ── Per-layer bbox history (WP-3 PR 3.2 — folded from RendererLayerHistory) ──
    //
    // Map from layer name to the previous frame's `LayerBBoxState`.  The
    // dirty-rect diff in `scene_dirty.cpp` reads `previous_layers.find(name)`
    // to compute per-layer diff.  This is a public field by design (see
    // `LayerBBoxState` rationale above).
    std::unordered_map<std::string, LayerBBoxState> previous_layers;

    /// Zero the per-frame telemetry counters; `previous_layers` is
    /// intentionally preserved.  Callers from `reset_frame_temporaries()`
    /// route through this method so the per-field set list lives in
    /// ONE place — future `DirtyHistory` field additions are picked up
    /// here without hunting through inline reset helpers.
    ///
    /// CONTRACT: if you ADD a field to `DirtyHistory` whose value is
    /// *frame-scoped* (i.e. per-frame telemetry like the seven above),
    /// you MUST register its default here.  `previous_layers` is the
    /// one and only field intentionally excluded — it carries state
    /// that survives cross-frame resets by design (the dirty-rect diff
    /// in `scene_dirty.cpp` reads it as the per-layer diff source).
    void reset_telemetry_counters() noexcept {
        last_dirty_area_ratio    = 1.0;
        last_layer_count         = 0;
        last_dirty_rect_enabled  = false;
        last_dirty_rect          = std::nullopt;
        last_tile_execution_used = false;
        last_fast_path_reused    = false;
        last_graph_reused        = false;
    }

    // ── Typed `previous_layers` accessors (WP-3 PR 3.2 polish) ──────
    //
    // The setter-exposure brief asks that callers (including tests
    // seeding non-default content) reach the per-layer state through
    // a typed accessor rather than touching the bare map.  These
    // helpers cover the common read / insert / clear patterns and are
    // THE SANCTIONED single-layer access path; the underlying
    // `previous_layers` map remains PUBLIC because two callers need
    // BULK access (SoftwareRenderer::commit_prev_frame_state moves a
    // complete rvalue map, and the dirty-rect diff in scene_dirty.cpp
    // iterates the full map).  Single-layer callers should prefer the
    // accessors below so the read / write contract is centralized.
    //
    // Note: these accessors are intentionally NOT `noexcept`.  The
    // `const std::string&` parameter can copy on entry, the
    // `std::unordered_map<std::string, …>::find` hashes via
    // `std::hash<std::string>` (not noexcept), and assignment can
    // rehash / allocate.  Weakening to non-noexcept is honest and
    // will not affect any production caller, but it matters for
    // the test lattice that was the original setter-exposure brief.
    [[nodiscard]] LayerBBoxState* previous_layer(const std::string& name) {
        auto it = previous_layers.find(name);
        return it == previous_layers.end() ? nullptr : &it->second;
    }
    [[nodiscard]] const LayerBBoxState* previous_layer(const std::string& name) const {
        auto it = previous_layers.find(name);
        return it == previous_layers.end() ? nullptr : &it->second;
    }
    void set_previous_layer(const std::string& name, const LayerBBoxState& state) {
        previous_layers[name] = state;
    }
    void clear_previous_layers() { previous_layers.clear(); }
};

} // namespace chronon3d

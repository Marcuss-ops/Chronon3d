#pragma once

// ---------------------------------------------------------------------------
// preflight_render_graph.hpp
//
// Pre-flight graph diagnostics: builds the render graph WITHOUT executing it
// and inspects every node for bbox, canvas intersection, visibility, cache
// flags, and dirty state.
//
// Primary entry point:
//
//   GraphPreflightReport report = debug_preflight_render_graph(
//       renderer, node_cache, scene, camera, W, H, frame, time_seconds,
//       renderer.render_settings(),
//       renderer.composition_registry(),
//       renderer.video_decoder()
//   );
//
//   // Human-readable dump for agents:
//   std::string text = report.to_text();
//
//   // Graphviz topology with bbox annotations:
//   std::string dot = report.dot;
//
// ---------------------------------------------------------------------------

#include <chronon3d/core/enum_utils.hpp>
#include <chronon3d/render_graph/render_pipeline.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::graph {

// ── Visibility classification ──────────────────────────────────────────────

enum class VisibilityStatus {
    FullyVisible,      ///< bbox fully inside [0,0,W,H]
    PartiallyClipped,  ///< bbox intersects but exceeds canvas boundary
    OutsideCanvas,     ///< bbox has zero intersection with canvas
    Unknown            ///< bbox could not be computed (e.g. full-canvas fallback)
};

[[nodiscard]] inline std::string to_string(VisibilityStatus s) {
    return enum_utils::enum_name_upper_snake(s);
}

// ── Per-node diagnostic record ─────────────────────────────────────────────

struct GraphPreflightNode {
    // Identity
    std::string name;          ///< node.name()
    std::string kind;          ///< to_string(node.kind())
    std::string layer_id;      ///< node.layer_id() — which scene layer owns this node

    // Spatial
    raster::BBox predicted_bbox{};    ///< predicted_bbox() in canvas coords
    raster::BBox canvas_bbox{};       ///< the target canvas [0,0,W,H]
    raster::BBox intersection_bbox{}; ///< predicted_bbox ∩ canvas_bbox

    float visible_ratio{0.0f};         ///< intersection_area / predicted_bbox_area  [0..1]

    bool outside_canvas{false};
    bool partially_clipped{false};

    VisibilityStatus visibility{VisibilityStatus::Unknown};

    // Cache / dirty state
    bool cacheable{false};        ///< node.cacheable()
    bool frame_dependent{false};  ///< node.frame_dependent()
    bool dirty{false};            ///< alias: frame_dependent (node will be re-evaluated)
    bool cached{false};           ///< alias: cacheable && !frame_dependent

    // Topology
    int input_count{0};   ///< number of edges coming into this node
    int output_count{0};  ///< number of nodes that consume this node

    // Warning produced for this node (empty = clean)
    std::string warning;

    // Advanced Diagnostics
    size_t predicted_memory_bytes{0};    ///< Estimated VRAM/RAM framebuffer footprint in bytes
    int complexity_score{0};              ///< Performance complexity weight score
    std::vector<std::string> dirty_reasons; ///< Detailed reasons/chain explaining why node is dirty
};

// ── Aggregate report ───────────────────────────────────────────────────────

struct GraphPreflightReport {
    /// Graphviz DOT string — enriched with bbox / layer_id / dirty annotations
    std::string dot;

    /// One entry per graph node (including Clear, Composite, Output…)
    std::vector<GraphPreflightNode> nodes;

    /// Flat list of all warnings (duplicates entries from node.warning)
    std::vector<std::string> warnings;

    // Advanced Diagnostics aggregates
    size_t peak_memory_bytes{0};          ///< Predicted peak memory usage during execution (bytes)
    int total_complexity_score{0};        ///< Total complexity score of the graph
    int cache_score{0};                    ///< Percentage of cached nodes in the graph [0..100]
    size_t total_fill_rate_pixels{0};     ///< Total pixel fill-rate across all nodes

    // ── Lookups ──────────────────────────────────────────────────────────────

    /// Find the first node whose name contains the given substring.
    [[nodiscard]] const GraphPreflightNode* find_node(std::string_view name) const;

    /// Returns true if any warning string contains the given substring.
    [[nodiscard]] bool has_warning_containing(std::string_view text) const;

    // ── Human-readable dump ──────────────────────────────────────────────────

    /// Produces a multi-line text report readable by AI agents or humans.
    /// Sections: header, per-node table (bbox, visibility, dirty), warning list.
    [[nodiscard]] std::string to_text() const;
};

// ── Main entry point ───────────────────────────────────────────────────────

/// Builds the render graph for the given scene at the given frame and
/// produces a pre-flight diagnostic report.
///
/// DOES NOT execute the render graph — no pixels are produced.
/// The function is safe to call before any actual rendering.
GraphPreflightReport debug_preflight_render_graph(
    RenderBackend&              backend,
    cache::NodeCache&           node_cache,
    const Scene&                scene,
    const Camera&               camera,
    i32 width,
    i32 height,
    Frame  frame,
    f32    frame_time,
    const RenderSettings&       settings,
    const CompositionRegistry*  registry,
    video::VideoFrameDecoder*   video_decoder,
    float fps = 30.0f
);

} // namespace chronon3d::graph

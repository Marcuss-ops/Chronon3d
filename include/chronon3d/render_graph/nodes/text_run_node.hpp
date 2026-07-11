#pragma once

// First-class render-graph node for batched text with After Effects-style
// per-glyph animation (GlyphSelector + TextAnimatorProperty stack).
//
// Distinct from source_node.hpp because:
//   1. The shape is a TextRunShape (immutable layout + animated glyph states),
//      not a Shape from shape.hpp.
//   2. The rendering backend is renderer::draw_text_run from
//      text_run_processor.hpp, not the shape-rasterizer pipeline.
//   3. Predicted bbox uses the 2.5D-aware
//      renderer::compute_text_run_world_bbox.
//
// Source-pass (graph_builder_source_pass.cpp) emits a TextRunNode instead
// of a SourceNode whenever the source RenderNode is flagged with
// is_text_run_shape = true and text_run_shape != nullptr.

#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <memory>
#include <span>
#include <string>
#include <optional>

namespace chronon3d::graph {

/// Resolved placement for a TextRun — the graph builder pre-computes
/// the final world matrix (including canvas-centre and 2.5D
/// projection if applicable).  TextRunNode receives this and only
/// applies SSAA scaling during rasterization.
struct TextRunPlacement {
    Mat4 matrix;
};

class TextRunNode final : public RenderGraphNode {
public:
    /// Builds a TextRunNode from:
    ///   - `shape`            : the TextRunShape holding immutable layout +
    ///                          per-glyph animated state.
    ///   - `render_ref`       : the underlying RenderNode (for shadow/glow
    ///                          spread + shape hash in cache_key).
    ///   - `key`              : skeleton cache key (scope + frame + size).
    ///   - `placement`        : pre-resolved world matrix (graph builder
    ///                          already applied canvas-centre + 2.5D
    ///                          projection if needed; TextRunNode only
    ///                          applies SSAA on top).
    ///   - `opacity_override` : modular-coordinates opacity plumbing.
    /// Cache policy: pass `frame_variant_cache("text_run")` for animated
    /// text, `static_memory_cache("text_run")` for static.
    TextRunNode(
        std::string name,
        std::shared_ptr<TextRunShape> shape,
        const ::chronon3d::RenderNode& render_ref,
        const cache::NodeCacheKey& key,
        TextRunPlacement placement,
        std::optional<f32> opacity_override = std::nullopt,
        RenderNodeCachePolicy policy = static_memory_cache("text_run")
    );

    // ── RenderGraphNode overrides ────────────────────────────────────────
    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::TextRun; }
    std::string_view name() const noexcept override { return m_name; }

    /// TextRuns are NOT full-frame seeds (the run shape itself is constrained).
    [[nodiscard]] bool can_seed_full_frame(const RenderGraphContext&) const noexcept override {
        return false;
    }


    /// 2.5D-aware predicted bbox using `compute_text_run_world_bbox`.
    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> input_bboxes = {}
    ) const override;

    /// Compute the full layer → canvas world matrix used by both
    /// `predicted_bbox()` and `execute()`. Exposed so external audit
    /// tools (e.g. `chronon3d_cli inspect-text`) can pass the same
    /// matrix to `audit_text_visibility()`.
    [[nodiscard]] Mat4 world_matrix(const RenderGraphContext& ctx) const;

    /// Cache key combines:
    ///   - skeleton key (from source pass: scope, frame, size)
    ///   - `hash_text_run_shape(*m_shape)` (layout + per-glyph state + material)
    ///   - placement hash (node name + position)
    ///   - placement.matrix hash
    ///   - opacity_override bytes (when present)
    ///   - 2.5D camera position/rotation/zoom/fov (when projected)
    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override;

    /// Render path:
    ///   1. Acquire a full-canvas framebuffer (no clear skip — text glyphs
    ///      can't seed a full frame).
    ///   2. Resolve the SoftwareRenderer via dynamic_cast on the RenderBackend
    ///      (matches the pattern used by graph_builder_layer_pipeline_pass.cpp
    ///      for the matte sub-pipeline).
    ///   3. Build a TextRunDrawParams (model_matrix, opacity).
    ///   4. Call `renderer::draw_text_run` for the batched rasterization.
    NodeExecResult execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef> inputs,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) override;

    // ── Accessors used by the source pass and tests ──────────────────────
    const ::chronon3d::RenderNode& render_node() const { return m_render_ref; }
    const std::shared_ptr<TextRunShape>& shape() const { return m_shape; }

private:
    std::string m_name;
    std::shared_ptr<TextRunShape> m_shape;
    ::chronon3d::RenderNode m_render_ref;
    cache::NodeCacheKey m_key;
    TextRunPlacement m_placement;
    std::optional<f32> m_opacity_override;

    // ── Fase A6 (DONE) — node immutability ───────────────────────────
    // m_shape is READ-ONLY after construction.  Per-frame glyph
    // evaluation happens on a LOCAL clone inside execute(), so two
    // concurrent frames on different workers never race on the same
    // glyph vector.  The immutable layout (shared_ptr<const>) is
    // shared across frames without copying.
};

} // namespace chronon3d::graph

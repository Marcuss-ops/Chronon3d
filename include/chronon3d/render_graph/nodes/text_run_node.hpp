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

class TextRunNode final : public RenderGraphNode {
public:
    /// Builds a TextRunNode from:
    ///   - `shape`            : the TextRunShape holding immutable layout +
    ///                          per-glyph animated state.  Mutated per-frame
    ///                          by `evaluate_animator_stack` (writes
    ///                          into `shape->glyphs`).
    ///   - `render_ref`       : the underlying RenderNode carrying position,
    ///                          opacity, transform override plumbing.
    ///   - `key`              : skeleton cache key (scope + frame + size);
    ///                          `cache_key()` will fill in content hash.
    ///   - `centered`         : canvas centroid (matches SourceNode).
    ///   - `uses_2_5d_projection` : 2.5D camera pipeline branch.
    ///   - `matrix_override`  / `opacity_override` : modular-coordinates plumbing.
    /// Cache policy is decided at construction time — pass a
    /// `frame_variant_cache("text_run")` for animated text,
    /// `static_memory_cache("text_run")` (default) for static.
    TextRunNode(
        std::string name,
        std::shared_ptr<TextRunShape> shape,
        const ::chronon3d::RenderNode& render_ref,
        const cache::NodeCacheKey& key,
        bool centered = false,
        bool uses_2_5d_projection = false,
        std::optional<Mat4> matrix_override = std::nullopt,
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


    [[nodiscard]] RenderNodeCachePolicy cache_policy() const noexcept override {
        return m_cache_policy;
    }

    /// 2.5D-aware predicted bbox using `compute_text_run_world_bbox`.
    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> input_bboxes = {}
    ) const override;

    /// Cache key combines:
    ///   - skeleton key (from source pass: scope, frame, size)
    ///   - `hash_text_run_shape(*m_shape)` (layout + per-glyph state + material)
    ///   - placement hash (node name + position)
    ///   - matrix_override / opacity_override bytes (when present)
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
    OwnedFB execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef> inputs,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) override;

    // ── Accessors used by the source pass and tests ──────────────────────
    const ::chronon3d::RenderNode& render_node() const { return m_render_ref; }
    const std::shared_ptr<TextRunShape>& shape() const { return m_shape; }
    bool uses_2_5d_projection() const { return m_uses_2_5d_projection; }

private:
    std::string m_name;
    std::shared_ptr<TextRunShape> m_shape;
    ::chronon3d::RenderNode m_render_ref;
    cache::NodeCacheKey m_key;
    bool m_centered{false};
    bool m_uses_2_5d_projection{false};
    std::optional<Mat4> m_matrix_override;
    std::optional<f32> m_opacity_override;
    RenderNodeCachePolicy m_cache_policy{static_memory_cache("text_run")};

    // ── Log throttle ───────────────────────────────────────────────────
    // Backend-mismatch diagnostic fires once per node lifetime at error
    // level, then falls back to debug to keep telemetry quiet at 60 fps.
    mutable bool m_backend_warned{false};
};

} // namespace chronon3d::graph

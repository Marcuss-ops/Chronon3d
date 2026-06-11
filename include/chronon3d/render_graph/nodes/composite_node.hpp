#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <span>

namespace chronon3d::graph {

class CompositeNode final : public RenderGraphNode {
public:
    bool cacheable() const override { return m_cache_frame >= 0; }

    CompositeNode(::chronon3d::BlendMode mode, Frame cache_frame = Frame{-1}, float world_z = 0.0f)
        : m_mode(mode), m_cache_frame(cache_frame), m_world_z(world_z) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Composite; }
    std::string name() const override { return "Composite"; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext&,
        std::span<const std::optional<raster::BBox>> input_bboxes = {}
    ) const override {
        if (input_bboxes.empty()) return std::nullopt;
        if (input_bboxes.size() == 1) return input_bboxes[0];

        auto bottom = input_bboxes[0];
        auto top = input_bboxes[1];
        if (!bottom) return top;
        if (!top) return bottom;
        // Both inputs have valid (known) but empty bounding boxes.
        // Return a valid empty bbox (not nullopt) so the dirty-rect system
        // knows the output is empty rather than falling back to a full-frame render.
        if (bottom->is_empty() && top->is_empty()) {
            const i32 x = std::min(bottom->x0, top->x0);
            const i32 y = std::min(bottom->y0, top->y0);
            return raster::BBox{x, y, x, y};
        }

        raster::BBox union_box;
        union_box.x0 = std::min(bottom->x0, top->x0);
        union_box.y0 = std::min(bottom->y0, top->y0);
        union_box.x1 = std::max(bottom->x1, top->x1);
        union_box.y1 = std::max(bottom->y1, top->y1);
        return union_box;
    }

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "composite",
            .frame = m_cache_frame >= 0 ? m_cache_frame : Frame{0},
            .width = ctx.frame.width,
            .height = ctx.frame.height,
            .params_hash = static_cast<u64>(m_mode)
        };
    }

    OwnedFB execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef> inputs,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) override;

    // ── Accessor for graph optimization ─────────────────────────────────
    [[nodiscard]] BlendMode blend_mode() const { return m_mode; }

private:
    BlendMode m_mode;
    Frame m_cache_frame{-1};
    float m_world_z{0.0f};
};

} // namespace chronon3d::graph

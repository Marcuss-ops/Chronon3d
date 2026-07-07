#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <chronon3d/compositor/composite_operator.hpp>
#include <atomic>
#include <span>

namespace chronon3d::graph {

class CompositeNode final : public RenderGraphNode {
public:

    CompositeNode(::chronon3d::BlendMode mode,
                  Frame cache_frame = Frame{-1},
                  float world_z = 0.0f,
                  ::chronon3d::CompositeOperator op = ::chronon3d::CompositeOperator::SourceOver,
                  RenderNodeCachePolicy policy = static_memory_cache("composite"))
        : RenderGraphNode(policy), m_mode(mode), m_cache_frame(cache_frame), m_world_z(world_z), m_operator(op),
          m_unique_id(++s_counter) {}

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Composite; }
    std::string_view name() const noexcept override { return "Composite"; }

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

        // CompositeOperator-specific bbox:
        // SourceOver:    union(bottom, top)
        // Stencil:       intersection(bottom, top) — source masks backdrop
        // Silhouette:    bottom — only backdrop (source only subtracts)
        if (m_operator == ::chronon3d::CompositeOperator::SourceOver) {
            raster::BBox union_box;
            union_box.x0 = std::min(bottom->x0, top->x0);
            union_box.y0 = std::min(bottom->y0, top->y0);
            union_box.x1 = std::max(bottom->x1, top->x1);
            union_box.y1 = std::max(bottom->y1, top->y1);
            return union_box;
        }

        // Stencil/Silhouette: intersection for Stencil, bottom for Silhouette
        if (m_operator == ::chronon3d::CompositeOperator::StencilAlpha ||
            m_operator == ::chronon3d::CompositeOperator::StencilLuma) {
            raster::BBox inter_box;
            inter_box.x0 = std::max(bottom->x0, top->x0);
            inter_box.y0 = std::max(bottom->y0, top->y0);
            inter_box.x1 = std::min(bottom->x1, top->x1);
            inter_box.y1 = std::min(bottom->y1, top->y1);
            return inter_box;
        }

        // SilhouetteAlpha / SilhouetteLuma → bottom only
        return *bottom;
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        // TICKET-122 diagnostic: fold unique instance ID into the cache key
        // so distinct CompositeNodes can never hash-collide, and use the
        // current frame for frame-variant composites (m_cache_frame < 0)
        // so zoom-different frames can never collide either.
        const u64 params_hash = hash_combine(
            m_unique_id,
            hash_combine(
                static_cast<u64>(m_mode),
                static_cast<u64>(m_operator)
            )
        );
        return cache::NodeCacheKey{
            .scope = "composite",
            .frame = m_cache_frame >= 0 ? m_cache_frame : ctx.frame_input.frame,
            .width = ctx.frame_input.width,
            .height = ctx.frame_input.height,
            .params_hash = params_hash
        };
    }

    NodeExecResult execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef> inputs,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) override;

    // ── Accessors for graph optimization ────────────────────────────────
    [[nodiscard]] BlendMode blend_mode() const { return m_mode; }
    [[nodiscard]] ::chronon3d::CompositeOperator composite_operator() const { return m_operator; }

private:
    BlendMode                  m_mode;
    CompositeOperator          m_operator{CompositeOperator::SourceOver};
    Frame                      m_cache_frame{-1};
    float                      m_world_z{0.0f};
    u64                        m_unique_id{0};
    static inline std::atomic<u64> s_counter{0};
};

} // namespace chronon3d::graph

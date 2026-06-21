#pragma once

// ============================================================================
// per_pixel_dof_node.hpp — Per-pixel depth-of-field render graph node
//
// Post-composite DOF node that reads the per-pixel depth buffer populated
// by CompositeNode during compositing and applies a variable-radius disc
// blur based on compute_dof_blur_radius().
//
// This replaces the per-layer DofEffectNode for scene-level DOF where
// different layers get different blur amounts based on their world_z.
// ============================================================================

#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

#include <cmath>
#include <span>
#include <string>
#include <memory>

namespace chronon3d::graph {

class PerPixelDofNode final : public RenderGraphNode {
public:
    explicit PerPixelDofNode(Camera2_5DRuntime camera)
        : RenderGraphNode(frame_variant_cache("per_pixel_dof")), m_camera(std::move(camera)) {}

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Effect; }
    std::string_view name() const noexcept override { return "PerPixelDOF"; }


    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "per_pixel_dof",
            .frame = ctx.frame_input.frame,
            .width = ctx.frame_input.width,
            .height = ctx.frame_input.height,
            .params_hash = hash_combine(
                hash_combine(
                    hash_value(m_camera.dof.focus_z),
                    hash_value(m_camera.dof.aperture)),
                hash_value(m_camera.dof.max_blur))
        };
    }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> input_bboxes = {}
    ) const override {
        // DOF blur expands the bounding box by max_blur pixels.
        if (input_bboxes.empty() || !input_bboxes[0]) {
            return std::nullopt;
        }
        auto bbox = *input_bboxes[0];
        if (bbox.is_empty()) return bbox;

        const float blur = m_camera.dof.max_blur;
        if (blur <= 0.5f) return bbox;

        bbox.x0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.x0) - blur)));
        bbox.y0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.y0) - blur)));
        bbox.x1 = std::min(ctx.frame_input.width, static_cast<i32>(std::ceil(static_cast<f32>(bbox.x1) + blur)));
        bbox.y1 = std::min(ctx.frame_input.height, static_cast<i32>(std::ceil(static_cast<f32>(bbox.y1) + blur)));
        return bbox;
    }

    OwnedFB execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef> inputs,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) override;

    static std::unique_ptr<PerPixelDofNode> create(const Camera2_5DRuntime& cam) {
        return std::make_unique<PerPixelDofNode>(cam);
    }

private:
    Camera2_5DRuntime m_camera;
};

} // namespace chronon3d::graph

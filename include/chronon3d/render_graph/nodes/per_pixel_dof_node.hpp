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
#include <chronon3d/backends/software/utils/effects/per_pixel_dof.hpp>

#include <cmath>
#include <span>
#include <string>
#include <memory>

namespace chronon3d::graph {

class PerPixelDofNode final : public RenderGraphNode {
public:
    explicit PerPixelDofNode(Camera2_5DRuntime camera)
        : m_camera(std::move(camera)) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Effect; }
    std::string name() const override { return "PerPixelDOF"; }

    [[nodiscard]] bool cacheable() const override { return true; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "per_pixel_dof",
            .frame = frame_dependent() ? ctx.frame.frame : Frame{0},
            .width = ctx.frame.width,
            .height = ctx.frame.height,
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
        bbox.x1 = std::min(ctx.frame.width, static_cast<i32>(std::ceil(static_cast<f32>(bbox.x1) + blur)));
        bbox.y1 = std::min(ctx.frame.height, static_cast<i32>(std::ceil(static_cast<f32>(bbox.y1) + blur)));
        return bbox;
    }

    OwnedFB execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef> inputs,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) override {
        if (inputs.empty() || !inputs[0]) {
            auto empty = ctx.acquire_owned_fb(ctx.frame.width, ctx.frame.height);
            empty->clear(Color::transparent());
            return empty;
        }

        if (!m_camera.dof.enabled) {
            return ctx.acquire_owned_fb(*inputs[0]);
        }

        // Check that the depth buffer was populated during compositing
        if (ctx.telemetry.dof_depth.empty()) {
            // No depth data — fall through without blur
            return ctx.acquire_owned_fb(*inputs[0]);
        }

        auto result = ctx.acquire_owned_fb(*inputs[0]);

        // Determine clip region
        std::optional<raster::BBox> clip = ctx.tile.clip_rect;
        auto pred = predicted_bbox(ctx, input_bboxes);
        if (pred && clip) {
            clip->x0 = std::max(clip->x0, pred->x0);
            clip->y0 = std::max(clip->y0, pred->y0);
            clip->x1 = std::min(clip->x1, pred->x1);
            clip->y1 = std::min(clip->y1, pred->y1);
        }

        // Apply per-pixel DOF blur using the depth buffer populated by compositing
        renderer::apply_per_pixel_dof(*result, ctx.telemetry.dof_depth, m_camera.dof, m_camera.lens, clip);

        if (ctx.telemetry.counters) {
            ctx.telemetry.counters->effect_stack_calls.fetch_add(1, std::memory_order_relaxed);
            uint64_t area = static_cast<uint64_t>(ctx.frame.width) * ctx.frame.height;
            if (clip) {
                raster::BBox clipped = *clip;
                clipped.clip_to(ctx.frame.width, ctx.frame.height);
                area = clipped.is_empty() ? 0
                    : static_cast<uint64_t>(clipped.x1 - clipped.x0) * (clipped.y1 - clipped.y0);
            }
            ctx.telemetry.counters->effect_pixels.fetch_add(area, std::memory_order_relaxed);
        }

        return result;
    }

    static std::unique_ptr<PerPixelDofNode> create(const Camera2_5DRuntime& cam) {
        return std::make_unique<PerPixelDofNode>(cam);
    }

private:
    Camera2_5DRuntime m_camera;
};

} // namespace chronon3d::graph

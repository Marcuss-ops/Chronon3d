#pragma once

#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/effects/effect_ids.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>
#include <chronon3d/render_graph/render_backend.hpp>

#include <cmath>
#include <span>

namespace chronon3d::graph {

class DofEffectNode final : public RenderGraphNode {
public:
    DofEffectNode(Camera2_5DRuntime camera, float layer_world_z)
        : RenderGraphNode(frame_variant_cache("dof")), m_camera(std::move(camera)), m_layer_world_z(layer_world_z) {}

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Effect; }
    std::string_view name() const noexcept override { return "DOF"; }


    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        const float blur = compute_dof_blur_radius(m_camera.dof, m_camera.lens, m_layer_world_z,
                                                  static_cast<f32>(ctx.frame_input.width));

        return cache::NodeCacheKey{
            .scope = "dof",
            .frame = ctx.frame_input.frame,
            .width = ctx.frame_input.width,
            .height = ctx.frame_input.height,
            .params_hash = hash_combine(
                hash_combine(
                    hash_combine(
                        hash_combine(0, hash_value(m_layer_world_z)),
                        hash_value(m_camera.dof.focus_z)),
                    hash_value(m_camera.dof.aperture)),
                hash_combine(hash_value(m_camera.dof.max_blur), hash_value(blur))
            )
        };
    }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> input_bboxes = {}
    ) const override {
        if (input_bboxes.empty() || !input_bboxes[0]) {
            return std::nullopt;
        }
        auto bbox = *input_bboxes[0];
        if (bbox.is_empty()) {
            return bbox;
        }
        const float blur = compute_dof_blur_radius(m_camera.dof, m_camera.lens, m_layer_world_z,
                                                  static_cast<f32>(ctx.frame_input.width));
        if (blur <= 0.5f) {
            return bbox;
        }
        bbox.x0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.x0) - blur)));
        bbox.y0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.y0) - blur)));
        bbox.x1 = std::min(ctx.frame_input.width, static_cast<i32>(std::ceil(static_cast<f32>(bbox.x1) + blur)));
        bbox.y1 = std::min(ctx.frame_input.height, static_cast<i32>(std::ceil(static_cast<f32>(bbox.y1) + blur)));
        return bbox;
    }

    OwnedFB execute(RenderGraphContext& ctx, std::span<const FramebufferRef> inputs, std::span<const std::optional<raster::BBox>> input_bboxes) override {
        if (inputs.empty() || !inputs[0]) {
            auto empty = ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
            empty->clear(Color::transparent());
            return empty;
        }

        const float blur = compute_dof_blur_radius(m_camera.dof, m_camera.lens, m_layer_world_z,
                                                  static_cast<f32>(ctx.frame_input.width));
        if (blur <= 0.5f) {
            return ctx.acquire_owned_fb(*inputs[0]);
        }

        auto result = ctx.acquire_owned_fb(*inputs[0]);
        if (ctx.services.backend) {
            EffectStack dof_stack;
            dof_stack.push_back(EffectInstance{
                effects::EffectDescriptor{.id = std::string{effects::ids::BlurGaussian}},
                BlurParams{blur}
            });
            std::optional<raster::BBox> local_clip = ctx.node_exec.clip_rect;
            auto pred_bbox = predicted_bbox(ctx, input_bboxes);
            if (pred_bbox) {
                if (local_clip) {
                    local_clip->x0 = std::max(local_clip->x0, pred_bbox->x0);
                    local_clip->y0 = std::max(local_clip->y0, pred_bbox->y0);
                    local_clip->x1 = std::min(local_clip->x1, pred_bbox->x1);
                    local_clip->y1 = std::min(local_clip->y1, pred_bbox->y1);
                } else {
                    local_clip = pred_bbox;
                }
            }
            const effects::EffectExecutionContext dof_context{
                .time_seconds = ctx.frame_input.time_seconds,
                .frame = ctx.frame_input.frame,
                .clip = local_clip,
                .quality = effects::RenderQuality::Final,
                .diagnostics_enabled = ctx.policy.diagnostics_enabled
            };
            ctx.services.backend->apply_effect_stack(*result, dof_stack, dof_context);
            if (ctx.node_exec.counters) {
                ctx.node_exec.counters->effect_stack_calls.fetch_add(1, std::memory_order_relaxed);
                uint64_t area = static_cast<uint64_t>(ctx.frame_input.width * ctx.frame_input.height);
                if (local_clip) {
                    raster::BBox clipped = *local_clip;
                    clipped.clip_to(ctx.frame_input.width, ctx.frame_input.height);
                    if (!clipped.is_empty()) {
                        area = static_cast<uint64_t>(clipped.x1 - clipped.x0) * (clipped.y1 - clipped.y0);
                    } else {
                        area = 0;
                    }
                }
                ctx.node_exec.counters->effect_pixels.fetch_add(area, std::memory_order_relaxed);
            }
        }
        return result;
    }

    static std::unique_ptr<DofEffectNode> create(const Camera2_5DRuntime& cam, float layer_world_z) {
        return std::make_unique<DofEffectNode>(cam, layer_world_z);
    }

private:
    Camera2_5DRuntime m_camera;
    float m_layer_world_z;
};

} // namespace chronon3d::graph

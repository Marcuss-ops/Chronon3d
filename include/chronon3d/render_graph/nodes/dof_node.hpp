#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/scene/camera/dof.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include <cmath>

namespace chronon3d::graph {

class DofEffectNode final : public RenderGraphNode {
public:
    DofEffectNode(Camera2_5DRuntime camera, float layer_world_z)
        : m_camera(std::move(camera)), m_layer_world_z(layer_world_z) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Effect; }
    std::string name() const override { return "DOF"; }

    [[nodiscard]] bool cacheable() const override { return true; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        const float blur = compute_dof_blur_radius(m_camera.dof, m_layer_world_z);

        return cache::NodeCacheKey{
            .scope = "dof",
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
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

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, const std::vector<std::shared_ptr<Framebuffer>>& inputs) override {
        if (inputs.empty()) return std::make_shared<Framebuffer>(ctx.width, ctx.height);
        
        const float blur = compute_dof_blur_radius(m_camera.dof, m_layer_world_z);

        auto result = std::make_shared<Framebuffer>(*inputs[0]);
        if (blur > 0.5f && ctx.renderer) {
            EffectStack dof_stack;
            dof_stack.push_back(EffectInstance{EffectParams{BlurParams{blur}}});
            ctx.renderer->apply_effect_stack(*result, dof_stack);
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

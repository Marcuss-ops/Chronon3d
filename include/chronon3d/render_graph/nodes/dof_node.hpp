#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

namespace chronon3d::graph {

class DofEffectNode final : public RenderGraphNode {
public:
    DofEffectNode(Camera2_5DRuntime camera, float layer_depth)
        : m_camera(std::move(camera)), m_layer_depth(layer_depth) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Effect; }
    std::string name() const override { return "DOF"; }

    [[nodiscard]] bool cacheable() const override { return true; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        // Blur amount depends on camera DOF params and depth
        const float world_z = m_layer_depth + m_camera.position.z;
        const float dist = std::abs(world_z - m_camera.dof.focus_z);
        const float blur = std::min(dist * m_camera.dof.aperture, m_camera.dof.max_blur);

        return cache::NodeCacheKey{
            .scope = "dof",
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = static_cast<uint64_t>(blur * 1000.0f) 
        };
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, const std::vector<std::shared_ptr<Framebuffer>>& inputs) override {
        if (inputs.empty()) return std::make_shared<Framebuffer>(ctx.width, ctx.height);
        
        const float world_z = m_layer_depth + m_camera.position.z;
        const float dist = std::abs(world_z - m_camera.dof.focus_z);
        const float blur = std::min(dist * m_camera.dof.aperture, m_camera.dof.max_blur);

        auto result = std::make_shared<Framebuffer>(*inputs[0]);
        if (blur > 0.5f && ctx.renderer) {
            EffectStack dof_stack;
            dof_stack.push_back(EffectInstance{EffectParams{BlurParams{blur}}});
            ctx.renderer->apply_effect_stack(*result, dof_stack);
        }
        return result;
    }

    static std::unique_ptr<DofEffectNode> create(const Camera2_5DRuntime& cam, float depth) {
        return std::make_unique<DofEffectNode>(cam, depth);
    }

private:
    Camera2_5DRuntime m_camera;
    float m_layer_depth;
};

} // namespace chronon3d::graph

#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/rendering/lighting_eval.hpp>

#include <string>
#include <utility>

namespace chronon3d::graph {

class LightingNode final : public RenderGraphNode {
public:
    LightingNode(std::string layer_name, Mat4 world_matrix, Material2_5D material)
        : m_layer_name(std::move(layer_name))
        , m_world_matrix(world_matrix)
        , m_material(material) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Effect; }
    std::string name() const override { return "Lighting"; }

    [[nodiscard]] bool cacheable() const override { return true; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        const u64 light_hash = rendering::hash_light_context(ctx.light_context);
        const u64 material_hash = rendering::hash_material_2_5d(m_material);
        const u64 world_hash = hash_bytes(&m_world_matrix[0][0], sizeof(Mat4));

        return cache::NodeCacheKey{
            .scope = "lighting:" + m_layer_name,
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = hash_combine(hash_combine(light_hash, material_hash), world_hash)
        };
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx,
                                         const std::vector<std::shared_ptr<Framebuffer>>& inputs) override {
        if (inputs.empty()) {
            return std::make_shared<Framebuffer>(ctx.width, ctx.height);
        }

        auto result = std::make_shared<Framebuffer>(*inputs[0]);
        if (!ctx.light_context.enabled || !m_material.accepts_lights) {
            return result;
        }

        const Vec3 normal_world = rendering::transform_normal(m_world_matrix, {0.0f, 0.0f, 1.0f});

        for (i32 y = 0; y < result->height(); ++y) {
            Color* row = result->pixels_row(y);
            for (i32 x = 0; x < result->width(); ++x) {
                if (row[x].a <= 0.0f) {
                    continue;
                }
                row[x] = rendering::evaluate_lighting(row[x], normal_world, ctx.light_context, m_material);
            }
        }

        return result;
    }

    static std::unique_ptr<LightingNode> create(std::string layer_name, const Mat4& world_matrix,
                                                 const Material2_5D& material) {
        return std::make_unique<LightingNode>(std::move(layer_name), world_matrix, material);
    }

private:
    std::string m_layer_name;
    Mat4 m_world_matrix{1.0f};
    Material2_5D m_material{};
};

} // namespace chronon3d::graph

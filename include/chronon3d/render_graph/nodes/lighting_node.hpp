#pragma once

#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/rendering/lighting_eval.hpp>

#include <string>
#include <utility>
#include <span>

namespace chronon3d::graph {

class LightingNode final : public RenderGraphNode {
public:
    LightingNode(std::string layer_name, Mat4 world_matrix, Material2_5D material)
        : RenderGraphNode(frame_variant_cache("lighting"))
        , m_layer_name(std::move(layer_name))
        , m_world_matrix(world_matrix)
        , m_material(material) {}

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Effect; }
    std::string_view name() const noexcept override { return "Lighting"; }
    bool has_compiled_recorder() const noexcept override { return true; }


    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        const u64 light_hash = rendering::hash_light_context(ctx.frame_input.light_context);
        const u64 material_hash = rendering::hash_material_2_5d(m_material);
        const u64 world_hash = hash_bytes(&m_world_matrix[0][0], sizeof(Mat4));

        return cache::NodeCacheKey{
            .scope = "lighting:" + m_layer_name,
            .frame = ctx.frame_input.frame,
            .width = ctx.frame_input.width,
            .height = ctx.frame_input.height,
            .params_hash = hash_combine(hash_combine(light_hash, material_hash), world_hash)
        };
    }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext&,
        std::span<const std::optional<raster::BBox>> input_bboxes = {}
    ) const override {
        if (input_bboxes.empty() || !input_bboxes[0]) {
            return std::nullopt;
        }
        return input_bboxes[0];
    }

    NodeExecResult execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef> inputs,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) override;

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

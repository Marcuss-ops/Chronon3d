#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/scene/layer/transition.hpp>
#include <span>

namespace chronon3d::graph {

class TransitionNode final : public RenderGraphNode {
public:
    TransitionNode(std::string layer_name, LayerTransitionSpec spec, bool is_out, float progress)
        : m_layer_name(std::move(layer_name)), m_spec(std::move(spec)), m_is_out(is_out), m_progress(progress) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Transition; }
    std::string name() const override { return "Transition (" + m_spec.transition_id + ")"; }

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameDependent;
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        cache::NodeCacheKey key{
            .scope = "transition:" + m_layer_name,
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
        };
        key.params_hash = hash_string(m_spec.transition_id);
        key.params_hash = hash_combine(key.params_hash, static_cast<u64>(m_is_out));
        key.params_hash = hash_combine(key.params_hash, hash_bytes(&m_progress, sizeof(f32)));
        key.params_hash = hash_combine(key.params_hash, static_cast<u64>(m_spec.direction));
        return key;
    }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) const override {
        if (!input_bboxes.empty()) {
            return input_bboxes[0];
        }
        return raster::BBox{0, 0, ctx.width, ctx.height};
    }

    std::shared_ptr<Framebuffer> execute(
        RenderGraphContext& ctx,
        std::span<const std::shared_ptr<Framebuffer>> inputs,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) override;

private:
    std::string m_layer_name;
    LayerTransitionSpec m_spec;
    bool m_is_out{false};
    float m_progress{0.0f};
};

} // namespace chronon3d::graph

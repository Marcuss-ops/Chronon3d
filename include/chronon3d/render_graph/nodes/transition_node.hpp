#pragma once

#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/render_graph/transition/transition_catalog.hpp>
#include <chronon3d/scene/model/core/transition.hpp>
#include <span>

namespace chronon3d::graph {

class TransitionNode final : public RenderGraphNode {
public:
    TransitionNode(std::string layer_name, LayerTransitionSpec spec, bool is_out,
                   Frame layer_from, Frame layer_duration)
        : RenderGraphNode(frame_variant_cache("transition"))
        , m_layer_name(std::move(layer_name)), m_spec(std::move(spec)), m_is_out(is_out),
          m_layer_from(layer_from), m_layer_duration(layer_duration) {
        m_full_name = "Transition (" + m_spec.transition_id + ")";
    }

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Transition; }
    std::string_view name() const noexcept override { return m_full_name; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override;

    [[nodiscard]] float compute_progress(const RenderGraphContext& ctx) const;

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) const override;

    NodeExecResult execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef> inputs,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) override;

private:
    std::string m_layer_name;
    LayerTransitionSpec m_spec;
    bool m_is_out{false};
    Frame m_layer_from{0};
    Frame m_layer_duration{-1};
    std::string m_full_name;
    mutable std::unique_ptr<LayerTransitionProgram> m_program;

    [[nodiscard]] const LayerTransitionProgram& resolve_program(const RenderGraphContext& ctx) const;
};


namespace transition_math {

inline float smoothstep01(float val) {
    return val * val * (3.0f - 2.0f * val);
}

float procedural_remotion_mask(float u, float v, float t, float seed);
float remotion_mask(float u, float v, float t, float speed, float direction);
void evaluate_remotion_color(float u, float v, float t, float speed, float direction, float angle,
                             float& out_r, float& out_g, float& out_b);

} // namespace transition_math

} // namespace chronon3d::graph

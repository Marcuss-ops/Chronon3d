#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <spdlog/spdlog.h>
#include <span>
#include <utility>

namespace chronon3d::graph {

class SourceNode final : public RenderGraphNode {
public:
    SourceNode(std::string name, const ::chronon3d::RenderNode& node, const cache::NodeCacheKey& key,
               bool centered = false, bool uses_2_5d_projection = false, std::optional<Mat4> matrix_override = std::nullopt,
               std::optional<f32> opacity_override = std::nullopt,
               RenderNodeCachePolicy policy = static_memory_cache("source"))
        : RenderGraphNode(policy)
        , m_name(std::move(name)), m_node(node), m_key(key), m_centered(centered), m_uses_2_5d_projection(uses_2_5d_projection),
          m_matrix_override(matrix_override), m_opacity_override(opacity_override) {}

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Source; }
    std::string_view name() const noexcept override { return m_name; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> = {}
    ) const override;



    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override;

    NodeExecResult execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef>,
        std::span<const std::optional<raster::BBox>>
    ) override;

    [[nodiscard]] bool can_seed_full_frame(const RenderGraphContext& ctx) const noexcept override;

    const ::chronon3d::RenderNode& render_node() const { return m_node; }
    bool uses_2_5d_projection() const { return m_uses_2_5d_projection; }

    void refresh(
        std::string name,
        const ::chronon3d::RenderNode& node,
        const cache::NodeCacheKey& key,
        bool centered = false,
        bool uses_2_5d_projection = false,
        std::optional<Mat4> matrix_override = std::nullopt,
        std::optional<f32> opacity_override = std::nullopt,
        RenderNodeCachePolicy policy = static_memory_cache("source")
    ) {
        m_name = std::move(name);
        m_node = node;
        m_key = key;
        m_centered = centered;
        m_uses_2_5d_projection = uses_2_5d_projection;
        m_matrix_override = std::move(matrix_override);
        m_opacity_override = std::move(opacity_override);
        if (m_cache_policy.mode != policy.mode || m_cache_policy.invalidation != policy.invalidation) {
            spdlog::warn("[source_node] cache policy changed from {}/{} to {}/{} — compiled graph must be invalidated",
                         static_cast<int>(m_cache_policy.mode), m_cache_policy.reason,
                         static_cast<int>(policy.mode), policy.reason);
        }
        m_cache_policy = policy;
    }


private:
    std::string m_name;
    ::chronon3d::RenderNode m_node;
    cache::NodeCacheKey m_key;
    bool m_centered{false};
    bool m_uses_2_5d_projection{false};
    std::optional<Mat4> m_matrix_override;
    std::optional<f32> m_opacity_override;
};

} // namespace chronon3d::graph

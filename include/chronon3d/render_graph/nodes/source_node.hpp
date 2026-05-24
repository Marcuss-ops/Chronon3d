#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <span>

namespace chronon3d::graph {

class SourceNode final : public RenderGraphNode {
public:
    SourceNode(std::string name, const ::chronon3d::RenderNode& node, const cache::NodeCacheKey& key,
               bool centered = false, bool is_3d = false, std::optional<Mat4> matrix_override = std::nullopt,
               std::optional<f32> opacity_override = std::nullopt, bool cache_static = false)
        : m_name(std::move(name)), m_node(node), m_key(key), m_centered(centered), m_is_3d(is_3d),
          m_matrix_override(matrix_override), m_opacity_override(opacity_override), m_cache_static(cache_static) {}

    bool cacheable() const override { return m_cache_static; }
    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Source; }
    std::string name() const override { return m_name; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> = {}
    ) const override;

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    [[nodiscard]] RenderNodeCachePolicy cache_policy() const override {
        if (m_cache_static) {
            return static_memory_cache("source_static");
        }
        return RenderNodeCachePolicy{
            .cacheable = true,
            .frame_dependent = true,
            .frame_invariant = false,
            .disk_cacheable = false,
            .lifetime = CacheLifetime::PerFrame,
            .invalidation = CacheInvalidation::WhenParamsChange,
            .debug_reason = "source_animated"
        };
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override;

    std::shared_ptr<Framebuffer> execute(
        RenderGraphContext& ctx,
        std::span<const std::shared_ptr<Framebuffer>>,
        std::span<const std::optional<raster::BBox>>
    ) override;

    [[nodiscard]] bool can_seed_full_frame(const RenderGraphContext& ctx) const override;

private:
    std::string m_name;
    ::chronon3d::RenderNode m_node;
    cache::NodeCacheKey m_key;
    bool m_centered{false};
    bool m_is_3d{false};
    std::optional<Mat4> m_matrix_override;
    std::optional<f32> m_opacity_override;
    bool m_cache_static{false};
};

} // namespace chronon3d::graph

#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <span>
#include <vector>

namespace chronon3d::graph {

struct MultiSourceItem {
    const ::chronon3d::RenderNode* node{nullptr};
    Mat4 matrix;
    f32 opacity;
};

class MultiSourceNode final : public RenderGraphNode {
public:
    MultiSourceNode(std::string name, std::vector<MultiSourceItem> items, const cache::NodeCacheKey& key,
                    bool centered = false, bool is_3d = false, bool cache_static = false);

    bool cacheable() const override { return m_cache_static; }
    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Source; }
    std::string name() const override { return m_name; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> = {}
    ) const override;

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override;
    [[nodiscard]] RenderNodeCachePolicy cache_policy() const override;

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override;

    OwnedFB execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef>,
        std::span<const std::optional<raster::BBox>>
    ) override;

    [[nodiscard]] bool can_seed_full_frame(const RenderGraphContext& ctx) const override { return false; }

    const std::vector<MultiSourceItem>& items() const { return m_items; }
    bool is_3d() const { return m_is_3d; }

    /// Returns true if this node represents a single full-frame image source.
    /// Used by the graph builder for skip-when-opaque analysis.
    [[nodiscard]] bool is_single_full_frame_image() const { return m_items.size() == 1 && m_cache_static && !m_is_3d; }

private:
    std::string m_name;
    std::vector<MultiSourceItem> m_items;
    cache::NodeCacheKey m_key;
    bool m_centered{false};
    bool m_is_3d{false};
    bool m_cache_static{false};
};

} // namespace chronon3d::graph

#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <spdlog/spdlog.h>
#include <span>
#include <vector>
#include <utility>

namespace chronon3d::graph {

struct MultiSourceItem {
    const ::chronon3d::RenderNode* node{nullptr};
    Mat4 matrix;
    f32 opacity;
};

class MultiSourceNode final : public RenderGraphNode {
public:
    MultiSourceNode(std::string name, std::vector<MultiSourceItem> items, const cache::NodeCacheKey& key,
                    bool centered = false, bool uses_2_5d_projection = false,
                    RenderNodeCachePolicy policy = static_memory_cache("multi_source"));

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

    [[nodiscard]] bool can_seed_full_frame(const RenderGraphContext& ctx) const noexcept override { return false; }

    const std::vector<MultiSourceItem>& items() const { return m_items; }
    bool uses_2_5d_projection() const { return m_uses_2_5d_projection; }

    void refresh(
        std::string name,
        std::vector<MultiSourceItem> items,
        const cache::NodeCacheKey& key,
        bool centered = false,
        bool uses_2_5d_projection = false,
        RenderNodeCachePolicy policy = static_memory_cache("multi_source")
    ) {
        m_name = std::move(name);
        m_items = std::move(items);
        m_key = key;
        m_centered = centered;
        m_uses_2_5d_projection = uses_2_5d_projection;
        if (m_cache_policy.mode != policy.mode || m_cache_policy.invalidation != policy.invalidation) {
            spdlog::warn("[multi_source_node] cache policy changed from {}/{} to {}/{} — compiled graph must be invalidated",
                         static_cast<int>(m_cache_policy.mode), m_cache_policy.reason,
                         static_cast<int>(policy.mode), policy.reason);
        }
        m_cache_policy = policy;
    }

    /// Returns true if this node represents a single full-frame image source.
    /// Used by the graph builder for skip-when-opaque analysis.
    [[nodiscard]] bool is_single_full_frame_image() const { return m_items.size() == 1 && cache_policy().reusable_across_frames() && !m_uses_2_5d_projection; }

private:
    std::string m_name;
    std::vector<MultiSourceItem> m_items;
    cache::NodeCacheKey m_key;
    bool m_centered{false};
    bool m_uses_2_5d_projection{false};

    // ── Fase A6 (DONE) — node immutability ───────────────────────────
    // TextRunShape items inside m_items are READ-ONLY after construction.
    // Per-frame glyph evaluation happens on a LOCAL clone inside execute(),
    // so two concurrent frames on different workers never race on the same
    // glyph vector.  The m_backend_warned mutable throttle was removed in
    // Fase A4+A5 (nodes return NodeExecutionError immediately on failure).
};

} // namespace chronon3d::graph

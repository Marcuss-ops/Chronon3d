#pragma once

#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/scene/model/layer/track_matte.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <algorithm>
#include <cmath>
#include <span>

namespace chronon3d::graph {

// Applies a track matte:
//   input[0] = target layer framebuffer
//   input[1] = matte layer framebuffer
// Output = target FB with alpha modulated by matte alpha or luma.
//
// Coordinate fix: uses canvas-relative coordinates to correctly map
// between target and matte origins.  See track_matte_node.cpp.
class TrackMatteNode final : public RenderGraphNode {
public:
    TrackMatteNode(TrackMatteType type,
                   std::string    target_name,
                   cache::NodeCacheKey key)
        : m_type(type), m_name("matte:" + target_name), m_key(std::move(key)) {}

    RenderGraphNodeKind kind()   const noexcept override { return RenderGraphNodeKind::TrackMatte; }
    std::string_view     name()   const noexcept override { return m_name; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext&,
        std::span<const std::optional<raster::BBox>> input_bboxes = {}
    ) const override {
        if (input_bboxes.empty() || !input_bboxes[0]) {
            return std::nullopt;
        }
        if (input_bboxes.size() < 2 || !input_bboxes[1]) {
            return input_bboxes[0];
        }
        // For Alpha/Luma → intersection(target, matte)
        // For AlphaInverted/LumaInverted → target (full coverage outside matte)
        if (m_type == TrackMatteType::AlphaInverted || m_type == TrackMatteType::LumaInverted) {
            return input_bboxes[0];
        }
        // Intersection for non-inverted
        const auto& target_bb = *input_bboxes[0];
        const auto& matte_bb  = *input_bboxes[1];
        return raster::BBox{
            .x0 = std::max(target_bb.x0, matte_bb.x0),
            .y0 = std::max(target_bb.y0, matte_bb.y0),
            .x1 = std::min(target_bb.x1, matte_bb.x1),
            .y1 = std::min(target_bb.y1, matte_bb.y1)
        };
    }

    [[nodiscard]] RenderNodeCachePolicy cache_policy() const noexcept override {
        return static_memory_cache("track_matte");
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext&) const override { return m_key; }

    OwnedFB execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef> inputs,
        std::span<const std::optional<raster::BBox>> input_bboxes
    ) override;

private:
    TrackMatteType      m_type;
    std::string         m_name;
    cache::NodeCacheKey m_key;
};

} // namespace chronon3d::graph

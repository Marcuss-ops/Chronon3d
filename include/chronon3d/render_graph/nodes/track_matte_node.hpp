#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/scene/layer/track_matte.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <algorithm>
#include <cmath>
#include <span>

namespace chronon3d::graph {

// Applies a track matte:
//   input[0] = target layer framebuffer
//   input[1] = matte layer framebuffer
// Output = target FB with alpha modulated by matte alpha or luma.
class TrackMatteNode final : public RenderGraphNode {
public:
    TrackMatteNode(TrackMatteType type,
                   std::string    target_name,
                   cache::NodeCacheKey key)
        : m_type(type), m_name("matte:" + target_name), m_key(std::move(key)) {}

    RenderGraphNodeKind kind()   const override { return RenderGraphNodeKind::TrackMatte; }
    std::string         name()   const override { return m_name; }

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext&) const override { return m_key; }

    std::shared_ptr<Framebuffer> execute(
        RenderGraphContext& ctx,
        std::span<const std::shared_ptr<Framebuffer>> inputs,
        std::span<const std::optional<raster::BBox>>
    ) override {
        if (inputs.size() < 2 || !inputs[0] || !inputs[1]) return inputs.empty() ? nullptr : inputs[0];

        const Framebuffer& target = *inputs[0];
        const Framebuffer& matte  = *inputs[1];

        auto out = ctx.acquire_framebuffer(target.width(), target.height());

        const int W = target.width();
        const int H = target.height();
        const int matte_w = matte.width();
        const int matte_h = matte.height();

        for (int y = 0; y < H; ++y) {
            const Color* target_row = target.pixels_row(y);
            const Color* matte_row = y < matte_h ? matte.pixels_row(y) : nullptr;
            Color* out_row = out->pixels_row(y);
            for (int x = 0; x < W; ++x) {
                Color tc = target_row[x];
                if (tc.a <= 0.0f) { out_row[x] = tc; continue; }

                const Color mc = (matte_row && x < matte_w) ? matte_row[x] : Color{0, 0, 0, 0};

                f32 mask = 1.0f;
                switch (m_type) {
                    case TrackMatteType::Alpha:
                        mask = mc.a;
                        break;
                    case TrackMatteType::AlphaInverted:
                        mask = 1.0f - mc.a;
                        break;
                    case TrackMatteType::Luma:
                        mask = (0.2126f * mc.r + 0.7152f * mc.g + 0.0722f * mc.b) * mc.a;
                        break;
                    case TrackMatteType::LumaInverted:
                        mask = 1.0f - ((0.2126f * mc.r + 0.7152f * mc.g + 0.0722f * mc.b) * mc.a);
                        break;
                    default: break;
                }

                f32 clamp_mask = std::clamp(mask, 0.0f, 1.0f);
                tc.r *= clamp_mask;
                tc.g *= clamp_mask;
                tc.b *= clamp_mask;
                tc.a *= clamp_mask;
                out_row[x] = tc;
            }
        }
        out->set_opaque(false);
        return out;
    }

private:
    TrackMatteType      m_type;
    std::string         m_name;
    cache::NodeCacheKey m_key;
};

} // namespace chronon3d::graph

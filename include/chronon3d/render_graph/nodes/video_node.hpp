#pragma once

#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/backends/video/video_frame_decoder.hpp>
#include <span>

namespace chronon3d::graph {

class VideoNode final : public RenderGraphNode {
public:
    VideoNode(video::VideoSource source, video::VideoFrameDecoder* decoder, Frame layer_start)
        : m_source(std::move(source)),
          m_decoder(decoder),
          m_layer_start(layer_start) {}

    [[nodiscard]] RenderGraphNodeKind kind() const override {
        return RenderGraphNodeKind::Video;
    }

    [[nodiscard]] std::string name() const override {
        return "Video:" + m_source.path;
    }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> = {}
    ) const override {
        const i32 render_w = m_source.size.x > 0.0f ? static_cast<i32>(m_source.size.x) : ctx.frame.frame.width;
        const i32 render_h = m_source.size.y > 0.0f ? static_cast<i32>(m_source.size.y) : ctx.frame.frame.height;
        return raster::BBox{0, 0, render_w, render_h};
    }

    [[nodiscard]] cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        const Frame local_frame = ctx.frame.frame.frame - m_layer_start;
        const Frame source_frame = video::map_video_frame(local_frame, m_source);

        const i32 render_w = m_source.size.x > 0.0f ? static_cast<i32>(m_source.size.x) : ctx.frame.frame.width;
        const i32 render_h = m_source.size.y > 0.0f ? static_cast<i32>(m_source.size.y) : ctx.frame.frame.height;
        return cache::NodeCacheKey{
            .scope = "video:" + m_source.path,
            .frame = source_frame,
            .width = render_w,
            .height = render_h,
            .params_hash = hash_video_source(m_source),
            .source_hash = hash_string(m_source.path)
        };
    }

    OwnedFB execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef>,
        std::span<const std::optional<raster::BBox>>
    ) override {
        if (!m_decoder) {
            const i32 render_w = m_source.size.x > 0.0f ? static_cast<i32>(m_source.size.x) : ctx.frame.frame.width;
            const i32 render_h = m_source.size.y > 0.0f ? static_cast<i32>(m_source.size.y) : ctx.frame.frame.height;
            return ctx.acquire_owned_fb(render_w, render_h);
        }

        const Frame local_frame = ctx.frame.frame.frame - m_layer_start;
        if (local_frame < 0) {
            return ctx.acquire_owned_fb(ctx.frame.frame.width, ctx.frame.frame.height);
        }

        const Frame source_frame = video::map_video_frame(local_frame, m_source);

        const i32 render_w = m_source.size.x > 0.0f ? static_cast<i32>(m_source.size.x) : ctx.frame.frame.width;
        const i32 render_h = m_source.size.y > 0.0f ? static_cast<i32>(m_source.size.y) : ctx.frame.frame.height;
        auto decoded = m_decoder->decode_frame(
            m_source.path,
            source_frame,
            render_w,
            render_h,
            m_source.source_fps
        );
        if (!decoded) {
            return ctx.acquire_owned_fb(render_w, render_h);
        }
        return ctx.acquire_owned_fb(*decoded);
    }

    const video::VideoSource& source() const { return m_source; }

private:
    video::VideoSource m_source;
    video::VideoFrameDecoder* m_decoder{};
    Frame m_layer_start{0};
};

} // namespace chronon3d::graph

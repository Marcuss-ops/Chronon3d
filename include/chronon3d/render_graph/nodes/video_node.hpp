#pragma once

#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/media/frame_source_provider.hpp>
#include <span>

namespace chronon3d::graph {

class VideoNode final : public RenderGraphNode {
public:
    VideoNode(video::VideoSource source, media::MediaFrameProvider* decoder, Frame layer_start)
        : RenderGraphNode(no_cache("video"))
        , m_source(std::move(source)),
          m_decoder(decoder),
          m_layer_start(layer_start),
          m_full_name("Video:" + m_source.path) {}

    [[nodiscard]] RenderGraphNodeKind kind() const noexcept override {
        return RenderGraphNodeKind::Video;
    }

    [[nodiscard]] std::string_view name() const noexcept override {
        return m_full_name;
    }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> = {}
    ) const override {
        const i32 render_w = m_source.size.x > 0.0f ? static_cast<i32>(m_source.size.x) : ctx.frame_input.width;
        const i32 render_h = m_source.size.y > 0.0f ? static_cast<i32>(m_source.size.y) : ctx.frame_input.height;
        return raster::BBox{0, 0, render_w, render_h};
    }

    [[nodiscard]] cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        const Frame local_frame = ctx.frame_input.frame - m_layer_start;
        const Frame source_frame = video::map_video_frame(local_frame, m_source);

        const i32 render_w = m_source.size.x > 0.0f ? static_cast<i32>(m_source.size.x) : ctx.frame_input.width;
        const i32 render_h = m_source.size.y > 0.0f ? static_cast<i32>(m_source.size.y) : ctx.frame_input.height;
        return cache::NodeCacheKey{
            .scope = "video:" + m_source.path,
            .frame = source_frame,
            .width = render_w,
            .height = render_h,
            .params_hash = hash_video_source(m_source),
            .source_hash = hash_string(m_source.path)
        };
    }

    NodeExecResult execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef>,
        std::span<const std::optional<raster::BBox>>
    ) override {
        if (!m_decoder) {
            const i32 render_w = m_source.size.x > 0.0f ? static_cast<i32>(m_source.size.x) : ctx.frame_input.width;
            const i32 render_h = m_source.size.y > 0.0f ? static_cast<i32>(m_source.size.y) : ctx.frame_input.height;
            return ctx.acquire_owned_fb(render_w, render_h);
        }

        const Frame local_frame = ctx.frame_input.frame - m_layer_start;
        if (local_frame < 0) {
            return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
        }

        const Frame source_frame = video::map_video_frame(local_frame, m_source);

        const i32 render_w = m_source.size.x > 0.0f ? static_cast<i32>(m_source.size.x) : ctx.frame_input.width;
        const i32 render_h = m_source.size.y > 0.0f ? static_cast<i32>(m_source.size.y) : ctx.frame_input.height;
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
    std::string m_full_name;  // "Video:" + path, stored for string_view
    media::MediaFrameProvider* m_decoder{};
    Frame m_layer_start{0};
};

} // namespace chronon3d::graph

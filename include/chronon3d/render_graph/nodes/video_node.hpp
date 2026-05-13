#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/video/video_source.hpp>
#include <chronon3d/video/video_decoder.hpp>

namespace chronon3d::graph {

class VideoNode final : public RenderGraphNode {
public:
    VideoNode(video::VideoSource source, video::VideoDecoder* decoder, Frame layer_start)
        : m_source(std::move(source)),
          m_decoder(decoder),
          m_layer_start(layer_start) {}

    [[nodiscard]] RenderGraphNodeKind kind() const override {
        return RenderGraphNodeKind::Video;
    }

    [[nodiscard]] std::string name() const override {
        return "Video:" + m_source.path;
    }

    [[nodiscard]] cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        const Frame local_frame = ctx.frame - m_layer_start;
        const Frame source_frame = video::map_video_frame(local_frame, m_source);

        return cache::NodeCacheKey{
            .scope = "video:" + m_source.path,
            .frame = source_frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = hash_video_source(m_source),
            .source_hash = rendergraph::hash_string(m_source.path)
        };
    }

    std::shared_ptr<Framebuffer> execute(
        RenderGraphContext& ctx,
        const std::vector<std::shared_ptr<Framebuffer>>&
    ) override {
        if (!m_decoder) {
            auto fb = std::make_shared<Framebuffer>(ctx.width, ctx.height);
            fb->clear(Color::transparent());
            return fb;
        }

        const Frame local_frame = ctx.frame - m_layer_start;
        if (local_frame < 0) {
            auto fb = std::make_shared<Framebuffer>(ctx.width, ctx.height);
            fb->clear(Color::transparent());
            return fb;
        }

        const Frame source_frame = video::map_video_frame(local_frame, m_source);

        return m_decoder->decode_frame(
            m_source.path,
            source_frame,
            ctx.width,
            ctx.height,
            m_source.source_fps
        );
    }

private:
    video::VideoSource m_source;
    video::VideoDecoder* m_decoder{};
    Frame m_layer_start{0};
};

} // namespace chronon3d::graph

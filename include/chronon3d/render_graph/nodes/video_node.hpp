#pragma once

#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/media/frame_source_provider.hpp>
#include <spdlog/spdlog.h>
#include <cmath>
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

    bool can_seed_full_frame(const RenderGraphContext& ctx) const noexcept override {
        if (ctx.frame_input.has_camera_2_5d) {
            return false;
        }
        if (ctx.node_exec.clip_rect) {
            const bool clip_is_full = ctx.node_exec.clip_rect->x0 <= 0 && ctx.node_exec.clip_rect->y0 <= 0 &&
                                      ctx.node_exec.clip_rect->x1 >= ctx.frame_input.width && ctx.node_exec.clip_rect->y1 >= ctx.frame_input.height;
            if (!clip_is_full) {
                return false;
            }
        }
        const i32 render_w = m_source.size.x > 0.0f ? static_cast<i32>(m_source.size.x) : ctx.frame_input.width;
        const i32 render_h = m_source.size.y > 0.0f ? static_cast<i32>(m_source.size.y) : ctx.frame_input.height;
        return render_w >= ctx.frame_input.width && render_h >= ctx.frame_input.height;
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
        auto* decoder = m_decoder ? m_decoder : ctx.services.video_decoder;
        if (!decoder) {
            if (ctx.policy.is_gpu_native_required()) {
                spdlog::error("[video-node] GPU_NATIVE_REQUIRED: no decoder wired for source='{}'", m_source.path);
                return NodeExecResult(NodeExecutionError{
                    .backend_code = RenderBackendErrorCode::ExecutionFailure,
                    .node_name = "video",
                    .message = "GPU_NATIVE_REQUIRED: no decoder wired for video source"});
            }
            spdlog::error("[video-node] no decoder wired for source='{}'", m_source.path);
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
        const i64 source_fps = std::max<i64>(
            1, static_cast<i64>(std::llround(m_source.source_fps)));
        auto decoded = decoder->decode_frame_at(
            m_source.path,
            RationalTime{source_frame.integral(), Rational{1, source_fps}},
            render_w,
            render_h
        );
        const auto* decoded_frame = media::decoded_frame_if(decoded);
        if (!decoded_frame || !decoded_frame->framebuffer) {
            if (media::decode_is_eos(decoded)) {
                spdlog::error("[video-node] decoder reached EOF before requested frame: path='{}' frame={}",
                              m_source.path, static_cast<int>(local_frame));
            } else if (const auto* failure = media::decode_failure_if(decoded)) {
                spdlog::error("[video-node] decoder failed: path='{}' frame={} reason={} message={}",
                              m_source.path, static_cast<int>(local_frame),
                              static_cast<int>(failure->diagnostic.reason),
                              failure->diagnostic.message);
            }
            if (ctx.policy.is_gpu_native_required()) {
                spdlog::error("[video-node] GPU_NATIVE_REQUIRED: decoder failed to return frame for path='{}' frame={}",
                              m_source.path, static_cast<int>(local_frame));
                return NodeExecResult(NodeExecutionError{
                    .backend_code = RenderBackendErrorCode::ExecutionFailure,
                    .node_name = "video",
                    .message = "GPU_NATIVE_REQUIRED: decoder returned no frame"});
            }
            spdlog::error("[video-node] decoder returned no frame: path='{}' frame={} source_frame={}",
                          m_source.path, static_cast<int>(local_frame),
                          static_cast<int>(source_frame));
            return ctx.acquire_owned_fb(render_w, render_h);
        }
        if (ctx.policy.is_gpu_native_required() &&
            decoded_frame->framebuffer->surface_handle() == runtime::kInvalidRenderSurfaceHandle) {
            spdlog::error("[video-node] GPU_NATIVE_REQUIRED: decoder returned CPU frame without native GPU surface: path='{}'",
                          m_source.path);
            return NodeExecResult(NodeExecutionError{
                .backend_code = RenderBackendErrorCode::ExecutionFailure,
                .node_name = "video",
                .message = "GPU_NATIVE_REQUIRED: video layer returned non-GPU native framebuffer"});
        }
        // Preserve the provider-owned native surface when possible. The
        // shared_ptr overload avoids an unnecessary CPU copy for GPU-backed
        // video frames; CPU providers retain the existing copy behavior.
        auto framebuffer = decoded_frame->framebuffer;
        return ctx.acquire_owned_fb(std::move(framebuffer));
    }

    const video::VideoSource& source() const { return m_source; }

private:
    video::VideoSource m_source;
    std::string m_full_name;  // "Video:" + path, stored for string_view
    media::MediaFrameProvider* m_decoder{};
    Frame m_layer_start{0};
};

} // namespace chronon3d::graph

// utils/video/native_video_frame_decoder.hpp — Production video-frame
// decoder for video source layers.
//
// The render graph's VideoNode consumes media::MediaFrameProvider to fetch a
// decoded framebuffer for a source at a given frame; until now the CLI render
// paths passed a null decoder, so every video layer (e.g. the golden
// VIDEO_BACKGROUND) rendered as an empty black framebuffer. This class is the
// missing production implementation: libavformat demux + libavcodec decode +
// swscale to RGBA, packed into the renderer Framebuffer with a bounded
// per-source frame cache.
//
// Compilation is gated on CHRONON3D_ENABLE_NATIVE_FFMPEG (VideoExport.cmake
// adds this translation unit and the chronon3d_ffmpeg_full link closure only
// when the flag is on); the stub keeps the header includable either way.
#pragma once

#include <chronon3d/media/frame_source_provider.hpp>
#include <chronon3d/core/profiling/render_counter_types.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/runtime/render_surface.hpp>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include "cuda_nv12_surface_compositor.hpp"
#endif

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}
#endif

namespace chronon3d::cli {

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

class NativeVideoFrameDecoder final : public media::MediaFrameProvider {
    struct Session;
public:
    NativeVideoFrameDecoder() = default;
    ~NativeVideoFrameDecoder() override;

    void set_counters(RenderCounters* counters) override { m_counters = counters; }
    void set_native_gpu_context(
        graph::RenderBackend* backend, runtime::RenderSurfaceRegistry* registry) override {
        m_backend = backend;
        m_surface_registry = registry;
    }

    /// Decode one frame from `path` at the given source frame index and pack
    /// it into a renderer Framebuffer at the source's native resolution
    /// (the layer transform applies fit/scaling, mirroring image layers).
    /// Returns nullptr on any decode failure — the VideoNode then renders a
    /// black framebuffer (fail-safe, never a crash).
    std::shared_ptr<Framebuffer> decode_frame(
        const std::string& path,
        Frame frame,
        int width,
        int height,
        float fps) override;

    std::shared_ptr<Framebuffer> try_native_frame(
        const std::shared_ptr<Session>& session, AVFrame* frame);

private:
    struct Session {
        // FFmpeg contexts are stateful per source. Keep contention local to
        // this session; the decoder-wide mutex only protects the session map.
        std::mutex mutex;
        AVFormatContext* fmt{nullptr};
        AVCodecContext* codec{nullptr};
        AVBufferRef* hw_device_ctx{nullptr};
        AVFrame* hw_transfer_frame{nullptr};
        SwsContext* sws{nullptr};
        int stream_index{-1};
        int64_t last_target{-1};
        std::vector<uint8_t> rgba;
        // Bounded decoded-frame cache keyed by source frame index (map is
        // ordered, so eviction drops the oldest entry first).
        std::map<int64_t, std::shared_ptr<Framebuffer>> cache;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
        runtime::RenderSurfaceHandle native_surface{runtime::kInvalidRenderSurfaceHandle};
        std::unique_ptr<CudaNv12SurfaceCompositor> native_compositor;
#endif

        ~Session();
    };

    std::mutex m_mutex;
    RenderCounters* m_counters{nullptr};
    graph::RenderBackend* m_backend{nullptr};
    runtime::RenderSurfaceRegistry* m_surface_registry{nullptr};
    std::map<std::string, std::shared_ptr<Session>> m_sessions;

    std::shared_ptr<Session> open_session_locked(const std::string& path);
};

#else  // CHRONON3D_ENABLE_NATIVE_FFMPEG

// Native FFmpeg disabled: video source layers stay unavailable (compile-time
// safety net; the CLI links this TU only when the flag is on).
class NativeVideoFrameDecoder final : public media::MediaFrameProvider {
public:
    std::shared_ptr<Framebuffer> decode_frame(
        const std::string&, Frame, int, int, float) override {
        return nullptr;
    }
};

#endif  // CHRONON3D_ENABLE_NATIVE_FFMPEG

}  // namespace chronon3d::cli

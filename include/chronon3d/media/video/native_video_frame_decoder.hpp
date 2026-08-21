// media/video/native_video_frame_decoder.hpp — Production video-frame
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
#include <chronon3d/backends/vulkan/cuda_nv12_surface_compositor.hpp>
#endif

#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}
#endif

namespace chronon3d::media {

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

class NativeVideoFrameDecoder final : public MediaFrameProvider {
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
        Session& session, AVFrame* frame);

private:
    struct Session {
        std::mutex mutex;
        std::mutex decode_mutex;
        AVFormatContext* fmt{nullptr};
        AVCodecContext* codec{nullptr};
        AVBufferRef* hw_device_ctx{nullptr};
        AVFrame* hw_transfer_frame{nullptr};
        AVFrame* decoded{nullptr};
        AVFrame* closest_frame{nullptr};
        AVPacket* packet{nullptr};
        SwsContext* sws{nullptr};
        int stream_index{-1};
        int64_t last_target{-1};
        // Once a sequential request reaches EOF, all later timeline frames
        // reuse the final decoded source frame.  Without this state every
        // tail frame re-read the container until EOF again.
        bool source_eof{false};
        int64_t eof_target{-1};
        std::shared_ptr<Framebuffer> eof_frame;
        std::vector<uint8_t> rgba;
        std::map<int64_t, std::shared_ptr<Framebuffer>> cache;

        static constexpr std::size_t kPrefetchCapacity = 4;
        struct PrefetchedFrame {
            int64_t target{-1};
            std::shared_ptr<Framebuffer> framebuffer;
        };
        std::deque<PrefetchedFrame> prefetch_queue;
        std::condition_variable prefetch_cv;
        std::atomic<bool> prefetch_stop{false};
        std::thread prefetch_worker;
        int64_t prefetch_next{-1};
        int64_t prefetch_inflight{-1};
        uint64_t prefetch_generation{0};

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
        static constexpr std::size_t kNativeDecodeSlots = 4;
        struct NativeDecodeSlot {
            runtime::RenderSurfaceHandle surface{runtime::kInvalidRenderSurfaceHandle};
            std::unique_ptr<backends::vulkan::CudaNv12SurfaceCompositor> compositor;
        };
        std::array<NativeDecodeSlot, kNativeDecodeSlots> native_slots;
        std::size_t next_native_slot{0};
        graph::RenderBackend* native_backend{nullptr};
        runtime::RenderSurfaceRegistry* native_surface_registry{nullptr};
#endif

        ~Session();
        void start_prefetch_worker(NativeVideoFrameDecoder* decoder);
    };

    std::mutex m_mutex;
    RenderCounters* m_counters{nullptr};
    graph::RenderBackend* m_backend{nullptr};
    runtime::RenderSurfaceRegistry* m_surface_registry{nullptr};
    std::map<std::string, std::shared_ptr<Session>> m_sessions;

    std::shared_ptr<Framebuffer> decode_frame_internal(
        Session& session, int64_t target);

    std::shared_ptr<Session> open_session_locked(const std::string& path);
};

#else  // CHRONON3D_ENABLE_NATIVE_FFMPEG

// Native FFmpeg disabled: video source layers stay unavailable (compile-time
// safety net; the CLI links this TU only when the flag is on).
class NativeVideoFrameDecoder final : public MediaFrameProvider {
public:
    std::shared_ptr<Framebuffer> decode_frame(
        const std::string&, Frame, int, int, float) override {
        return nullptr;
    }
};

#endif  // CHRONON3D_ENABLE_NATIVE_FFMPEG

}  // namespace chronon3d::media

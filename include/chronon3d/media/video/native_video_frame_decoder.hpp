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
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/media/video/native_frame_importer.hpp>
#include <chronon3d/media/video/hw_frame_ref.hpp>
#include <chronon3d/media/video/video_device_runtime.hpp>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavutil/frame.h>
}
#endif

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

/// Test-only injection point used by the teardown stress test
/// (tests/video/test_native_decoder_teardown_stress.cpp) to bisect which
/// decoder subsystem introduces heap corruption during concurrent
/// create/decode/destroy cycles. Each flag defaults to the production
/// value (`true`) so the struct is a no-op outside the stress harness.
///
/// The matrix is:
///   prefetch ON / swscale ON / cache ON   (production)
///   prefetch OFF / swscale ON / cache ON
///   prefetch OFF / swscale OFF / cache ON
///   prefetch OFF / swscale OFF / cache OFF
/// If a row passes where the previous row crashed, the disabled subsystem
/// is the corruption source.
struct NativeDecoderTestOptions {
    bool enable_prefetch{true};
    bool enable_swscale{true};
    bool enable_frame_cache{true};
};

/// Surface description for the CUDA-interop decode handoff (the surface
/// created by try_native_frame).
///
/// The CUDA side (CudaNv12SurfaceCompositor::composite -> nv12_to_rgba)
/// writes RGBA float4 — 16 bytes per pixel — into the imported Vulkan
/// surface via surf2Dwrite, and the render graph samples that surface as an
/// RGBA float storage image.  The surface must therefore always be allocated
/// as Rgba32Float.  Sizing it from the decoded YUV format (Nv12: 1.5 B/px,
/// P010: 3 B/px) lets those float4 writes overrun the external allocation by
/// ~5-10x and fault with CUDA_ERROR_ILLEGAL_ADDRESS at the CUDA/Vulkan
/// handoff.
[[nodiscard]] inline runtime::SurfaceDesc native_decode_surface_desc(
    std::uint32_t width, std::uint32_t height) noexcept {
    return runtime::SurfaceDesc{
        width,
        height,
        runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage,
        // Decode/import slots are owned by one render job. They must be
        // retired by the end-of-job Vulkan drain before CUDA destroys its
        // external semaphore imports; keeping them JobPersistent leaves the
        // Vulkan semaphore alive across the CUDA teardown boundary.
        runtime::LifetimeClass::FrameTransient,
        runtime::tight_surface_bytes(
            runtime::PixelFormat::Rgba32Float, width, height),
    };
}

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

class NativeVideoFrameDecoder final : public MediaFrameProvider {
    struct Session;
public:
    NativeVideoFrameDecoder() = default;
    ~NativeVideoFrameDecoder() override;

    void set_counters(RenderCounters* counters) override { m_counters = counters; }
    void set_native_frame_importer(
        std::shared_ptr<NativeFrameImporter> importer) override {
        m_native_importer = std::move(importer);
    }
    /// Bind NVDEC to the process-persistent video device runtime. The
    /// decoder then borrows the SAME FFmpeg CUDA hwdevice (and therefore the
    /// same primary CUDA context) as the encoder — one context per device.
    void set_video_runtime(std::shared_ptr<VideoDeviceRuntime> runtime) {
        m_video_runtime = std::move(runtime);
    }

    void set_gpu_hot_path_mode(GpuHotPathMode mode) override {
        m_gpu_hot_path_mode = mode;
    }

    /// Trace correlation context: stable per-job id mixed with the decoded
    /// source frame to build the Perfetto flow id for this decode. Set once
    /// before decoding starts (the prefetch worker may read it concurrently).
    void set_trace_job_id(std::uint64_t job_id) noexcept {
        m_trace_job_id.store(job_id, std::memory_order_relaxed);
    }

    /// Test-only: override production subsystem toggles for the teardown
    /// stress harness. Default-constructed options leave every subsystem
    /// enabled (production behavior). Production code never calls this.
    void set_test_options(NativeDecoderTestOptions opts) noexcept {
        m_test_options = opts;
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

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    /// Return one ref-counted CUDA-backed decoded frame without importing it
    /// into a Vulkan RGBA surface.  This is the input contract for the
    /// DirectCudaYuvProgram; callers own the returned frame reference.
    [[nodiscard]] HwFrameRef decode_native_frame(
        const std::string& path,
        Frame frame,
        int width,
        int height,
        float fps);
#endif

    std::shared_ptr<Framebuffer> try_native_frame(
        Session& session, AVFrame* frame);

    struct DecodeProfilingStats {
        uint64_t decoded_frames{0};
        double container_open_ms{0.0};
        double stream_probe_ms{0.0};
        double decoder_open_ms{0.0};
        double demux_read_packet_ms{0.0};
        double avcodec_send_packet_ms{0.0};
        double avcodec_receive_frame_ms{0.0};
        double nvdec_wait_ms{0.0};
        double decode_total_ms{0.0};
        std::vector<double> frame_durations_ms;
    };
    [[nodiscard]] DecodeProfilingStats decode_profiling_stats() const;

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
        cache::LruCache<int64_t, std::shared_ptr<Framebuffer>> cache{
            64, 1, cache::CapacityMode::Count
        };

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

        std::unique_ptr<NativeFrameImportSession> native_import_session;

        // Temporary hand-off used by DirectCudaYuvProgram.  The decoder owns
        // the reusable AVFrame, while this ref keeps the CUDA hw surface alive
        // after decode_frame_internal returns.  It is enabled only around a
        // direct-native request and never changes the ordinary graph path.
        bool capture_native_frame{false};
        HwFrameRef captured_native_frame;
        HwFrameRef eof_captured_native_frame;
        bool direct_prefetch_disabled{false};
        NativeDecoderTestOptions test_options;
        DecodeProfilingStats profiling;

        ~Session();
        void start_prefetch_worker(NativeVideoFrameDecoder* decoder);
    };

    std::mutex m_mutex;
    RenderCounters* m_counters{nullptr};
    std::shared_ptr<NativeFrameImporter> m_native_importer;
    std::shared_ptr<VideoDeviceRuntime> m_video_runtime;
    GpuHotPathMode m_gpu_hot_path_mode{GpuHotPathMode::Auto};
    std::atomic<std::uint64_t> m_trace_job_id{0};
    NativeDecoderTestOptions m_test_options;
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
    struct DecodeProfilingStats {
        std::uint64_t decoded_frames{0};
        double container_open_ms{0.0};
        double stream_probe_ms{0.0};
        double decoder_open_ms{0.0};
        double demux_read_packet_ms{0.0};
        double avcodec_send_packet_ms{0.0};
        double avcodec_receive_frame_ms{0.0};
        double nvdec_wait_ms{0.0};
        double decode_total_ms{0.0};
        std::vector<double> frame_durations_ms;
    };
    std::shared_ptr<Framebuffer> decode_frame(
        const std::string&, Frame, int, int, float) override {
        return nullptr;
    }
    void set_video_runtime(std::shared_ptr<VideoDeviceRuntime>) noexcept {}
    [[nodiscard]] HwFrameRef decode_native_frame(
        const std::string&, Frame, int, int, float) { return {}; }
    [[nodiscard]] DecodeProfilingStats decode_profiling_stats() const { return {}; }
    void set_trace_job_id(std::uint64_t) noexcept {}  // no-op stub
    void set_test_options(NativeDecoderTestOptions) noexcept {}  // no-op stub
};

#endif  // CHRONON3D_ENABLE_NATIVE_FFMPEG

}  // namespace chronon3d::media

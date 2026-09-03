#pragma once

#include <chronon3d/media/frame_source_provider.hpp>
#include <chronon3d/media/video/source_sample_table.hpp>
#include <chronon3d/core/profiling/render_counter_types.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/media/video/native_frame_importer.hpp>
#include <chronon3d/media/video/hw_frame_ref.hpp>
#include <chronon3d/media/video/video_device_runtime.hpp>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavutil/frame.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
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

namespace chronon3d::media {

struct NativeDecoderTestOptions {
    bool enable_prefetch{true};
    bool enable_swscale{true};
    bool enable_frame_cache{true};
};

[[nodiscard]] inline runtime::SurfaceDesc native_decode_surface_desc(
    std::uint32_t width, std::uint32_t height) noexcept {
    return runtime::SurfaceDesc::make(width, height, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage, runtime::LifetimeClass::FrameTransient);
}

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
class NativeVideoFrameDecoder final : public MediaFrameProvider {
    struct Session;
public:
    NativeVideoFrameDecoder() = default;
    ~NativeVideoFrameDecoder() override;

    void set_counters(RenderCounters* counters) override { m_counters = counters; }
    void set_native_frame_importer(std::shared_ptr<NativeFrameImporter> importer) override {
        m_native_importer = std::move(importer);
    }
    void set_video_runtime(std::shared_ptr<VideoDeviceRuntime> runtime) { m_video_runtime = std::move(runtime); }
    void set_gpu_hot_path_mode(GpuHotPathMode mode) override { m_gpu_hot_path_mode = mode; }
    void set_trace_job_id(std::uint64_t job_id) noexcept { m_trace_job_id.store(job_id, std::memory_order_relaxed); }
    void set_test_options(NativeDecoderTestOptions opts) noexcept { m_test_options = opts; }

    std::shared_ptr<Framebuffer> decode_frame_at(
        const std::string& path, RationalTime presentation_time,
        int width, int height) override;

    [[nodiscard]] HwFrameRef decode_native_frame_at(
        const std::string& path, RationalTime presentation_time, int width, int height);

    std::shared_ptr<Framebuffer> try_native_frame(Session& session, AVFrame* frame);

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
        SourceSampleTable sample_table{};
        int64_t last_sample_index{-1};
        std::vector<uint8_t> rgba;
        cache::LruCache<int64_t, std::shared_ptr<Framebuffer>> cache{64, 1, cache::CapacityMode::Count};

        static constexpr std::size_t kPrefetchCapacity = 4;
        struct PrefetchedFrame { int64_t target{-1}; std::shared_ptr<Framebuffer> framebuffer; };
        std::deque<PrefetchedFrame> prefetch_queue;
        std::condition_variable prefetch_cv;
        std::atomic<bool> prefetch_stop{false};
        std::thread prefetch_worker;
        int64_t prefetch_next{-1};
        int64_t prefetch_inflight{-1};
        uint64_t prefetch_generation{0};

        std::unique_ptr<NativeFrameImportSession> native_import_session;
        bool capture_native_frame{false};
        HwFrameRef captured_native_frame;
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

    std::shared_ptr<Framebuffer> decode_frame_internal(Session& session, int64_t sample_index);
    std::shared_ptr<Session> open_session_locked(const std::string& path);
};
#else
class NativeVideoFrameDecoder final : public MediaFrameProvider {
public:
    struct DecodeProfilingStats {
        std::uint64_t decoded_frames{0}; double container_open_ms{0.0}; double stream_probe_ms{0.0};
        double decoder_open_ms{0.0}; double demux_read_packet_ms{0.0}; double avcodec_send_packet_ms{0.0};
        double avcodec_receive_frame_ms{0.0}; double nvdec_wait_ms{0.0}; double decode_total_ms{0.0};
        std::vector<double> frame_durations_ms;
    };
    std::shared_ptr<Framebuffer> decode_frame_at(const std::string&, RationalTime, int, int) override { return nullptr; }
    void set_video_runtime(std::shared_ptr<VideoDeviceRuntime>) noexcept {}
    [[nodiscard]] HwFrameRef decode_native_frame_at(const std::string&, RationalTime, int, int) { return {}; }
    [[nodiscard]] DecodeProfilingStats decode_profiling_stats() const { return {}; }
    void set_trace_job_id(std::uint64_t) noexcept {}
    void set_test_options(NativeDecoderTestOptions) noexcept {}
};
#endif

} // namespace chronon3d::media

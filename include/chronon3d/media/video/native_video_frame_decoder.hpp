#pragma once

#include <chronon3d/media/frame_source_provider.hpp>
#include <chronon3d/media/media_io_budget.hpp>
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
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace chronon3d::color { class OcioColorTransform; }

namespace chronon3d::media {

struct NativeDecoderTestOptions {
    bool enable_prefetch{true};
    bool enable_swscale{true};
    bool enable_frame_cache{true};
};

[[nodiscard]] inline runtime::SurfaceDesc native_decode_surface_desc(
    std::uint32_t width, std::uint32_t height) noexcept {
    auto desc = runtime::SurfaceDesc::make(
        width, height, runtime::PixelFormat::Rgba32Float,
        runtime::ResourceUsage::Storage, runtime::LifetimeClass::JobPersistent);
    // Native decode imports NV12/P010 into a float4 RGBA CUDA/Vulkan view;
    // this is the explicit RGBA interop lane, never a YUV encode surface.
    desc.kind = runtime::ResourceKind::Color;
    return desc;
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
    /// Force the reference/software lane to avoid selecting a CUVID decoder.
    /// Native GPU export leaves this disabled and continues to use the shared
    /// registry-owned CUDA device when one is available.
    void set_software_only(bool software_only) noexcept { m_software_only = software_only; }
    void set_gpu_hot_path_mode(GpuHotPathMode mode) override { m_gpu_hot_path_mode = mode; }
    void set_trace_job_id(std::uint64_t job_id) noexcept { m_trace_job_id.store(job_id, std::memory_order_relaxed); }
    void set_test_options(NativeDecoderTestOptions opts) noexcept { m_test_options = opts; }

    DecodeResult decode_frame_at(
        const std::string& path, RationalTime presentation_time,
        int width, int height) override;
    DecodeResult decode_frame_at_with_color_transform(
        const std::string& path, RationalTime presentation_time,
        int width, int height, const VideoColorTransform& transform) override;

    /// Decode by the source table's presentation order. This preserves VFR
    /// sample timing and avoids synthesizing timestamps from average FPS.
    DecodeResult decode_frame_index_at(
        const std::string& path, std::int64_t sample_index,
        int width, int height);

    /// Exact number of indexed presentation samples in the opened source.
    [[nodiscard]] std::uint64_t indexed_sample_count(const std::string& path);

    /// Exact PTS and time base for one zero-based presentation-order sample.
    [[nodiscard]] std::optional<RationalTime> indexed_presentation_time(
        const std::string& path, std::uint64_t sample_index);

    [[nodiscard]] HwFrameRef decode_native_frame_at(
        const std::string& path, RationalTime presentation_time, int width, int height);

    std::shared_ptr<Framebuffer> try_native_frame(
        Session& session, AVFrame* frame, DecodeDiagnostic* diagnostic);

    struct DecodeProfilingStats {
        uint64_t decoded_frames{0};
        /// Number of requests satisfied from the retained-frame window
        /// instead of a keyframe seek + GOP re-decode (duplicate or
        /// slightly-out-of-order sample indices, e.g. two layers sharing one
        /// source path). Monotonic per session; deterministic regression
        /// coverage asserts this increments on replays and stays flat on new
        /// sample decodes.
        uint64_t retained_replays{0};
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
        // High-water-mark RGBA staging used by the CPU fallback. Keeping it
        // on the decoder session removes an av_image_alloc/free pair from
        // every decoded frame while preserving the existing framebuffer
        // format contract.
        std::vector<std::uint8_t> rgba_staging;
        /// Small window of recently decoded-and-materialized frames (sample
        /// index -> retained ref), so duplicate or slightly-out-of-order
        /// requests — FPS mapping, two layers sharing one source path — can
        /// be replayed without a keyframe seek + GOP re-decode.
        struct RetainedFrame {
            std::int64_t index{-1};
            AVFrame* frame{nullptr};
        };
        std::vector<RetainedFrame> retained_frames;
        static constexpr std::int64_t kRetainedWindowSize = 6;
        AVPacket* packet{nullptr};
        SwsContext* sws{nullptr};
        int stream_index{-1};
        SourceSampleTable sample_table{};
        int64_t last_sample_index{-1};
        cache::LruCache<int64_t, std::shared_ptr<Framebuffer>> cache{64, 1, cache::CapacityMode::Count};

        static constexpr std::size_t kPrefetchCapacity = 4;
        struct PrefetchedFrame {
            int64_t target{-1};
            std::shared_ptr<Framebuffer> framebuffer;
            MediaIoBudget::Reservation io_reservation{};
        };
        std::deque<PrefetchedFrame> prefetch_queue;
        std::condition_variable prefetch_cv;
        std::atomic<bool> prefetch_stop{false};
        std::atomic<bool> foreground_decode_pending{false};
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

    /// AUDIT-08B: mutable so const readers (decode_profiling_stats) can take
    /// the session-map lock without const_cast.
    mutable std::mutex m_mutex;
    RenderCounters* m_counters{nullptr};
    std::shared_ptr<NativeFrameImporter> m_native_importer;
    std::shared_ptr<VideoDeviceRuntime> m_video_runtime;
    bool m_software_only{false};
    GpuHotPathMode m_gpu_hot_path_mode{GpuHotPathMode::Auto};
    std::atomic<std::uint64_t> m_trace_job_id{0};
    NativeDecoderTestOptions m_test_options;
    std::map<std::string, std::shared_ptr<Session>> m_sessions;
#ifdef CHRONON3D_ENABLE_OCIO
    std::map<std::string, std::shared_ptr<color::OcioColorTransform>> m_ocio_transforms;
#endif

    DecodeResult decode_frame_internal(Session& session, int64_t sample_index
#ifdef CHRONON3D_ENABLE_OCIO
        , const color::OcioColorTransform* ocio_transform = nullptr
#endif
    );
    DecodeResult decode_frame_at_impl(const std::string& path,
        RationalTime presentation_time, int width, int height,
        const VideoColorTransform* transform);
    static void retain_materialized_frame(Session& session, std::int64_t index, AVFrame* frame);
    std::shared_ptr<Session> open_session_locked(
        const std::string& path, DecodeDiagnostic* diagnostic);
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
    DecodeResult decode_frame_at(const std::string&, RationalTime, int, int) override {
        return make_decode_failure(DecodeDiagnostic{DecodeFailureReason::DecoderUnavailable, 0,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "native FFmpeg decoder support is disabled"});
    }
    DecodeResult decode_frame_index_at(const std::string&, std::int64_t, int, int) {
        return make_decode_failure(DecodeDiagnostic{DecodeFailureReason::DecoderUnavailable, 0,
            kNoDecodeTimestamp, kNoDecodeTimestamp, 0,
            "native FFmpeg decoder support is disabled"});
    }
    [[nodiscard]] std::uint64_t indexed_sample_count(const std::string&) { return 0; }
    [[nodiscard]] std::optional<RationalTime> indexed_presentation_time(
        const std::string&, std::uint64_t) { return std::nullopt; }
    void set_video_runtime(std::shared_ptr<VideoDeviceRuntime>) noexcept {}
    void set_software_only(bool) noexcept {}
    [[nodiscard]] HwFrameRef decode_native_frame_at(const std::string&, RationalTime, int, int) { return {}; }
    [[nodiscard]] DecodeProfilingStats decode_profiling_stats() const { return {}; }
    void set_trace_job_id(std::uint64_t) noexcept {}
    void set_test_options(NativeDecoderTestOptions) noexcept {}
};
#endif

} // namespace chronon3d::media

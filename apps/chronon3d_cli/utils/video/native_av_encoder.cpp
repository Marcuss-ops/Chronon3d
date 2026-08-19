#include "native_av_encoder.hpp"
#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/render_counter_types.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/backends/vulkan/cuda_vulkan_surface_bridge.hpp>
#include <cuda.h>
#endif

// Convenience: steady clock helpers
namespace {
using Clock = std::chrono::steady_clock;
inline double elapsed_ms(const Clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

bool set_codec_option_checked(AVCodecContext* codec, const char* key,
                              const std::string& value) {
    const int rc = av_opt_set(codec->priv_data, key, value.c_str(), 0);
    if (rc < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(rc, error, sizeof(error));
        spdlog::error("[native_av] unsupported encoder option {}='{}': {}",
                      key, value, error);
        return false;
    }
    return true;
}
}

namespace chronon3d::cli {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Map our options to the codec name that avcodec_find_encoder_by_name expects.
static const char* resolve_encoder_name(const FfmpegPipeOptions& opt) {
    if (opt.hardware_encoder == "nvenc") {
        if (opt.codec == "hevc" || opt.codec == "libx265") return "hevc_nvenc";
        return "h264_nvenc";
    }
    if (opt.codec == "libx264rgb")
        return "libx264rgb";
    return "libx264";
}

// ---------------------------------------------------------------------------
// open
// ---------------------------------------------------------------------------

bool NativeAvEncoder::open(const FfmpegPipeOptions& options) {
    if (fmt_ || codec_) {
        spdlog::error("[native_av] Encoder already open");
        return false;
    }

    open_complete_ = false;

    if (options.width <= 0 || options.height <= 0 ||
        options.fps <= 0 || options.output_path.empty()) {
        spdlog::error("[native_av] Invalid encoder options (w={}, h={}, fps={}, path='{}')",
                      options.width, options.height, options.fps, options.output_path);
        return false;
    }

    options_ = options;
    frames_written_ = 0;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    frames_submitted_ = 0;
    cuda_pending_peak_ = 0;
    cuda_backpressure_wait_count_ = 0;
    pending_cuda_frames_.clear();
#endif

    // Reset conversion cache so a stale digest from a prior export won't
    // produce a false hit if the same object is reopened.
    last_converted_digest_       = 0;
    last_converted_width_        = 0;
    last_converted_height_       = 0;
    last_converted_pix_fmt_      = -1;
    last_converted_apply_gamma_  = false;
    last_converted_color_matrix_ = -1;
    cache_hits_   = 0;
    cache_misses_ = 0;

    // Reset telemetry accumulators
    native_convert_ms_         = 0.0;
    native_send_frame_ms_      = 0.0;
    native_backpressure_ms_    = 0.0;
    native_flush_ms_           = 0.0;
    native_receive_packet_ms_  = 0.0;
    native_mux_write_ms_       = 0.0;
    native_trailer_ms_         = 0.0;

    // 1. Allocate format context (MP4 muxer)
    const std::string filename = options_.output_path;
    avformat_alloc_output_context2(&fmt_, nullptr, nullptr, filename.c_str());
    if (!fmt_) {
        spdlog::error("[native_av] avformat_alloc_output_context2 failed for '{}'", filename);
        return false;
    }

    // 2. Find the encoder
    const AVCodec* encoder = avcodec_find_encoder_by_name(resolve_encoder_name(options_));
    if (!encoder) {
        spdlog::error("[native_av] avcodec_find_encoder_by_name('{}') failed", resolve_encoder_name(options_));
        return false;
    }

    // 3. Allocate codec context
    codec_ = avcodec_alloc_context3(encoder);
    if (!codec_) {
        spdlog::error("[native_av] avcodec_alloc_context3 failed");
        return false;
    }

    codec_->width     = options_.width;
    codec_->height    = options_.height;
    codec_->time_base = AVRational{1, options_.fps};
    codec_->framerate = AVRational{options_.fps, 1};
    // GOP size = 1 second (fps frames) for reliable decoder compatibility.
    // With fps*2, the very subtle tracking_breathing animation (4% scale over
    // 120 frames) produces P-frames with 99.7% skip on the "ultrafast" preset
    // — some H.264 decoders can't decode those near-empty P-frames, resulting
    // in visible frames only at keyframe intervals.  fps*2 worked for most
    // content but broke for near-static scenes with tiny per-frame changes.
    codec_->gop_size  = options_.fps;  // was fps * 2
    codec_->max_b_frames = 0;              // no B-frames for lowest latency

    // NVENC consumes CUDA frames. The GPU path is enabled only for the
    // native NVENC profile; all other encoders retain the CPU YUV contract.
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    gpu_nvenc_ = options_.hardware_encoder == "nvenc";
    if (gpu_nvenc_) {
        if (cuInit(0) != CUDA_SUCCESS) {
            spdlog::error("[native_av] CUDA driver initialization failed");
            return false;
        }
        CUdevice cuda_device{};
        if (cuDeviceGet(&cuda_device, 0) != CUDA_SUCCESS) return false;
#if defined(CUDA_VERSION) && CUDA_VERSION >= 13000
        CUcontext context = nullptr;
        if (cuCtxCreate(&context, nullptr, 0, cuda_device) != CUDA_SUCCESS) return false;
        cuda_context_ = context;
#else
        CUcontext context = nullptr;
        if (cuCtxCreate(&context, 0, cuda_device) != CUDA_SUCCESS) return false;
        cuda_context_ = context;
#endif
        if (cuStreamCreate(&cuda_stream_, CU_STREAM_NON_BLOCKING) != CUDA_SUCCESS) {
            spdlog::error("[native_av] CUDA stream creation failed");
            return false;
        }
        if (av_hwdevice_ctx_create(&cuda_device_ref_, AV_HWDEVICE_TYPE_CUDA,
                                   "0", nullptr, 0) < 0) return false;
        cuda_frames_ref_ = av_hwframe_ctx_alloc(cuda_device_ref_);
        if (!cuda_frames_ref_) return false;
        auto* frames = reinterpret_cast<AVHWFramesContext*>(cuda_frames_ref_->data);
        frames->format = AV_PIX_FMT_CUDA;
        frames->sw_format = AV_PIX_FMT_BGR0;
        frames->width = options_.width;
        frames->height = options_.height;
        frames->initial_pool_size = 4;
        if (av_hwframe_ctx_init(cuda_frames_ref_) < 0) return false;
        codec_->pix_fmt = AV_PIX_FMT_CUDA;
        codec_->hw_frames_ctx = av_buffer_ref(cuda_frames_ref_);
    } else
#endif
    {
        codec_->pix_fmt = AV_PIX_FMT_YUV420P;
    }

    // Set encoder options (preset, crf, tune, threads)
    if (!options_.preset.empty()) {
        if (!set_codec_option_checked(codec_, "preset", options_.preset)) return false;
    }
    {
        char crf_str[16];
        snprintf(crf_str, sizeof(crf_str), "%d", options_.crf);
        // NVENC does not expose x264's CRF option; its quality control is
        // resolved through the NVENC-specific options below/defaults.
        if (!gpu_nvenc_ && !set_codec_option_checked(codec_, "crf", crf_str)) return false;
    }
    // Apply the unified CPU budget to x264.  encode_threads == 0 keeps the
    // legacy "auto" behaviour for backwards compatibility.
    const std::string codec_name = resolve_encoder_name(options_);
    if (codec_name == "libx264" || codec_name == "libx264rgb") {
        if (options_.encode_threads > 0) {
            char threads_str[16];
            snprintf(threads_str, sizeof(threads_str), "%d", options_.encode_threads);
            if (!set_codec_option_checked(codec_, "threads", threads_str)) return false;
        } else {
            if (!set_codec_option_checked(codec_, "threads", "auto")) return false;
        }
        if (!set_codec_option_checked(codec_, "thread_type", "frame")) return false;
    }
    // tune: default empty for batch export (faster), use "zerolatency" only for streaming
    const std::string tune = options_.tune.empty() ? "" : options_.tune;
        if (!tune.empty() && !gpu_nvenc_) {
        if (!set_codec_option_checked(codec_, "tune", tune)) return false;
    }

    // Global header if the container format requires it
    if (fmt_->oformat->flags & AVFMT_GLOBALHEADER) {
        codec_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // 4. Open the codec
    if (avcodec_open2(codec_, encoder, nullptr) < 0) {
        spdlog::error("[native_av] avcodec_open2 failed");
        return false;
    }

    // 5. Create stream
    stream_ = avformat_new_stream(fmt_, nullptr);
    if (!stream_) {
        spdlog::error("[native_av] avformat_new_stream failed");
        return false;
    }
    stream_->time_base = codec_->time_base;

    if (avcodec_parameters_from_context(stream_->codecpar, codec_) < 0) {
        spdlog::error("[native_av] avcodec_parameters_from_context failed");
        return false;
    }

    // 6. Open output file
    if (!(fmt_->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&fmt_->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0) {
            spdlog::error("[native_av] avio_open failed for '{}'", filename);
            return false;
        }
    }

    // 7. Write header (no +faststart for benchmarks — adds write-seek overhead)
    AVDictionary* mux_opts = nullptr;
    // No +faststart — we want clean close() timing for benchmarks.
    // faststart can be added in a follow-up when the user needs streaming MP4.
    if (avformat_write_header(fmt_, &mux_opts) < 0) {
        spdlog::error("[native_av] avformat_write_header failed");
        return false;
    }

    // 8. Allocate frame + packet
    frame_  = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!frame_ || !packet_) {
        spdlog::error("[native_av] av_frame_alloc or av_packet_alloc failed");
        return false;
    }

    frame_->format = codec_->pix_fmt;
    frame_->width  = codec_->width;
    frame_->height = codec_->height;

    if (!gpu_nvenc_ && av_frame_get_buffer(frame_, 32) < 0) {
        spdlog::error("[native_av] av_frame_get_buffer failed");
        return false;
    }

    spdlog::info("[native_av] Opened native encoder: {}x{} @ {}fps, codec={}, preset={}, crf={}, output='{}'",
                 options_.width, options_.height, options_.fps,
                 resolve_encoder_name(options_), options_.preset, options_.crf, filename);
    open_complete_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// close — drain encoder, write trailer, clean up
// ---------------------------------------------------------------------------

bool NativeAvEncoder::close() {
    if (!codec_) {
        // Already closed or never opened — not an error.
        if (!open_complete_) abort_open();
        return true;
    }
    open_complete_ = false;

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    // Complete the bounded CUDA->NVENC queue before flushing the codec. In
    // steady state this is polled asynchronously; only the final tail waits.
    if (gpu_nvenc_ && !drain_ready_cuda_frames(true)) {
        spdlog::error("[native_av] failed to drain pending CUDA frames");
    }
#endif

    // 1. Drain encoder: send NULL to flush internal buffers (encoder flush).
    const auto t_flush0 = Clock::now();
    avcodec_send_frame(codec_, nullptr);

    // 2. Receive and write remaining packets (timing tracked inside drain_packets)
    drain_packets();
    const double flush_ms = elapsed_ms(t_flush0);
    native_flush_ms_ += flush_ms;

    // 3. Write trailer (finalizes the MP4 file, writes moov atom, etc.)
    //    This is the mux finalization, measured separately from the encoder
    //    flush so the two tails never mask each other.
    const auto t_trailer0 = Clock::now();
    if (fmt_) {
        av_write_trailer(fmt_);
    }
    const double trailer_ms = elapsed_ms(t_trailer0);

    native_trailer_ms_ += trailer_ms;

    if (profiling::g_current_counters) {
        profiling::g_current_counters->encoder_flush_wall_ms.fetch_add(
            static_cast<uint64_t>(flush_ms), std::memory_order_relaxed);
        profiling::g_current_counters->mux_finalize_wall_ms.fetch_add(
            static_cast<uint64_t>(trailer_ms), std::memory_order_relaxed);
    }

    // 5. Close the IO
    if (fmt_ && !(fmt_->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&fmt_->pb);
    }

    // 5. Free resources
    av_packet_free(&packet_);
    av_frame_free(&frame_);
    avcodec_free_context(&codec_);
    avformat_free_context(fmt_);

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    // Imported external memory and semaphores are reusable for the lifetime
    // of each persistent encode surface. Do not destroy them frame-by-frame:
    // that path causes driver object churn and defeats the interop ring.
    cuda_surface_bridges_.clear();
    if (cuda_stream_) {
        cuStreamDestroy(cuda_stream_);
        cuda_stream_ = nullptr;
    }
    av_buffer_unref(&cuda_frames_ref_);
    av_buffer_unref(&cuda_device_ref_);
    if (cuda_context_) {
        cuCtxDestroy(reinterpret_cast<CUcontext>(cuda_context_));
        cuda_context_ = nullptr;
    }
    gpu_nvenc_ = false;
#endif

    fmt_    = nullptr;
    codec_  = nullptr;
    stream_ = nullptr;
    frame_  = nullptr;
    packet_ = nullptr;

    spdlog::info("[native_av] Closed native encoder — {} frames written, YUV cache: {} hits / {} misses",
                 frames_written_, cache_hits_, cache_misses_);
    return true;
}

NativeAvEncoder::~NativeAvEncoder() {
    if (open_complete_) {
        close();
    } else {
        abort_open();
    }
}

void NativeAvEncoder::abort_open() noexcept {
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    for (auto& pending : pending_cuda_frames_) {
        if (pending.frame) av_frame_free(&pending.frame);
        if (pending.ready) {
            (void)cuEventDestroy(pending.ready);
            pending.ready = nullptr;
        }
    }
    pending_cuda_frames_.clear();
    if (cuda_context_) (void)cuCtxSetCurrent(reinterpret_cast<CUcontext>(cuda_context_));
    cuda_surface_bridges_.clear();
    if (cuda_stream_) {
        (void)cuStreamDestroy(cuda_stream_);
        cuda_stream_ = nullptr;
    }
#endif
    av_packet_free(&packet_);
    av_frame_free(&frame_);
    avcodec_free_context(&codec_);
    if (fmt_ && fmt_->pb && fmt_->oformat &&
        !(fmt_->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&fmt_->pb);
    }
    avformat_free_context(fmt_);
    fmt_ = nullptr;
    stream_ = nullptr;
    packet_ = nullptr;
    frame_ = nullptr;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    av_buffer_unref(&cuda_frames_ref_);
    av_buffer_unref(&cuda_device_ref_);
    if (cuda_context_) {
        (void)cuCtxDestroy(reinterpret_cast<CUcontext>(cuda_context_));
        cuda_context_ = nullptr;
    }
    gpu_nvenc_ = false;
#endif
    open_complete_ = false;
}

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
bool NativeAvEncoder::drain_ready_cuda_frames(bool wait_for_one) {
    while (!pending_cuda_frames_.empty()) {
        auto& pending = pending_cuda_frames_.front();
        const CUresult ready = cuEventQuery(pending.ready);
        if (ready == CUDA_ERROR_NOT_READY) {
            if (!wait_for_one) return true;
            const auto wait_t0 = Clock::now();
            if (cuEventSynchronize(pending.ready) != CUDA_SUCCESS) return false;
            native_backpressure_ms_ += elapsed_ms(wait_t0);
            ++cuda_backpressure_wait_count_;
        } else if (ready != CUDA_SUCCESS) {
            return false;
        }

        const auto send_t0 = Clock::now();
        const int send_ret = avcodec_send_frame(codec_, pending.frame);
        native_send_frame_ms_ += elapsed_ms(send_t0);
        av_frame_free(&pending.frame);
        cuEventDestroy(pending.ready);
        pending.ready = nullptr;
        pending_cuda_frames_.pop_front();
        if (send_ret < 0 || !drain_packets()) return false;
        ++frames_written_;
        wait_for_one = false;
    }
    return true;
}
#endif

bool NativeAvEncoder::write_native_surface(
    graph::RenderBackend& backend,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle destination) {
#ifndef CHRONON3D_ENABLE_CUDA_INTEROP
    (void)backend; (void)source; (void)destination;
    return false;
#else
    if (!gpu_nvenc_ || !cuda_context_ || !cuda_frames_ref_ ||
        source == runtime::kInvalidRenderSurfaceHandle ||
        destination == runtime::kInvalidRenderSurfaceHandle) {
        spdlog::error("[native_av] invalid GPU frame contract source={} destination={} gpu_nvenc={} cuda_context={} frames_ctx={}",
                      source, destination, gpu_nvenc_, cuda_context_ != nullptr,
                      cuda_frames_ref_ != nullptr);
        return false;
    }
    auto* vulkan = dynamic_cast<backends::vulkan::VulkanBackend*>(&backend);
    if (!vulkan) return false;
    const auto copy_result = vulkan->copy_surface_to_cuda_encoder(source, destination, false);
    if (!copy_result.ok()) {
        spdlog::error("[native_av] GPU surface conversion failed: {}", copy_result.error().message);
        return false;
    }
    try {
        if (!drain_ready_cuda_frames(false)) return false;
        if (pending_cuda_frames_.size() >= 3 &&
            !drain_ready_cuda_frames(true)) {
            spdlog::error("[native_av] CUDA encoder queue backpressure failed");
            return false;
        }
        const auto key = static_cast<std::uint64_t>(destination);
        auto bridge_it = cuda_surface_bridges_.find(key);
        if (bridge_it == cuda_surface_bridges_.end()) {
            const auto info = vulkan->export_cuda_external_memory(destination);
            auto bridge = std::make_unique<backends::vulkan::CudaVulkanSurfaceBridge>(
                info, reinterpret_cast<CUcontext>(cuda_context_));
            bridge_it = cuda_surface_bridges_.emplace(key, std::move(bridge)).first;
        }
        auto& bridge = *bridge_it->second;
        bridge.wait_for_vulkan(cuda_stream_);

        AVFrame* gpu_frame = av_frame_alloc();
        if (!gpu_frame) return false;
        gpu_frame->format = AV_PIX_FMT_CUDA;
        gpu_frame->width = options_.width;
        gpu_frame->height = options_.height;
        gpu_frame->hw_frames_ctx = av_buffer_ref(cuda_frames_ref_);
        if (av_hwframe_get_buffer(cuda_frames_ref_, gpu_frame, 0) < 0) {
            spdlog::error("[native_av] av_hwframe_get_buffer failed for GPU frame {}", frames_written_);
            av_frame_free(&gpu_frame);
            return false;
        }
        CUDA_MEMCPY2D copy{};
        copy.srcMemoryType = CU_MEMORYTYPE_ARRAY;
        copy.srcArray = bridge.array();
        copy.dstMemoryType = CU_MEMORYTYPE_DEVICE;
        copy.dstDevice = reinterpret_cast<CUdeviceptr>(gpu_frame->data[0]);
        copy.srcPitch = static_cast<std::size_t>(options_.width) * 4;
        copy.dstPitch = gpu_frame->linesize[0];
        copy.WidthInBytes = static_cast<std::size_t>(options_.width) * 4;
        copy.Height = options_.height;
        if (cuMemcpy2DAsync(&copy, cuda_stream_) != CUDA_SUCCESS) {
            spdlog::error("[native_av] cuMemcpy2DAsync failed for GPU frame {}", frames_submitted_);
            av_frame_free(&gpu_frame);
            return false;
        }
        CUevent ready = nullptr;
        if (cuEventCreate(&ready, CU_EVENT_DISABLE_TIMING) != CUDA_SUCCESS ||
            cuEventRecord(ready, cuda_stream_) != CUDA_SUCCESS) {
            if (ready) cuEventDestroy(ready);
            spdlog::error("[native_av] CUDA completion event failed for GPU frame {}",
                          frames_submitted_);
            av_frame_free(&gpu_frame);
            return false;
        }
        gpu_frame->pts = static_cast<int64_t>(frames_submitted_++);
        pending_cuda_frames_.push_back(PendingCudaFrame{gpu_frame, ready});
        cuda_pending_peak_ = std::max<std::uint64_t>(
            cuda_pending_peak_, pending_cuda_frames_.size());
        if (counters_) {
            counters_->gpu_surface_copy_frames.fetch_add(1, std::memory_order_relaxed);
            counters_->encoder_staging_copy_bytes.fetch_add(
                static_cast<std::uint64_t>(options_.width) * options_.height * 4,
                std::memory_order_relaxed);
        }
        // Poll only; the bounded queue applies backpressure on the oldest
        // event when it reaches three frames in flight.
        if (!drain_ready_cuda_frames(false)) return false;
        return true;
    } catch (const std::exception& error) {
        spdlog::error("[native_av] CUDA/Vulkan frame handoff failed: {}", error.what());
        return false;
    }
#endif
}

// ---------------------------------------------------------------------------
// drain_packets — common helper used during write and close
// ---------------------------------------------------------------------------

bool NativeAvEncoder::drain_packets() {
    if (!codec_ || !fmt_ || !stream_ || !packet_) {
        return false;
    }

    for (;;) {
        const auto t_recv0 = Clock::now();
        int ret = avcodec_receive_packet(codec_, packet_);
        const double recv_ms = elapsed_ms(t_recv0);
        native_receive_packet_ms_ += recv_ms;

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // No more packets available right now (or stream fully drained)
            return true;
        }
        if (ret < 0) {
            spdlog::error("[native_av] avcodec_receive_packet error: {}", ret);
            return false;
        }

        // libx264 emits PTS but no per-packet duration; the MP4 muxer
        // derives the container duration from the last packet's
        // pts + duration, so without an explicit duration the final
        // frame's display time is dropped (a 90-frame @30fps segment
        // reports 2.967s instead of 3.0s — Test 7).  One frame in codec
        // time_base {1, fps} units.
        if (packet_->duration <= 0) {
            packet_->duration = 1;
        }

        // Rescale timestamps to stream time base and mux
        av_packet_rescale_ts(packet_, codec_->time_base, stream_->time_base);
        packet_->stream_index = stream_->index;

        const auto t_mux0 = Clock::now();
        ret = av_interleaved_write_frame(fmt_, packet_);
        const double mux_ms = elapsed_ms(t_mux0);
        native_mux_write_ms_ += mux_ms;

        av_packet_unref(packet_);

        if (ret < 0) {
            spdlog::error("[native_av] av_interleaved_write_frame error: {}", ret);
            return false;
        }

        if (profiling::g_current_counters) {
            profiling::g_current_counters->native_av_receive_packet_wall_ms.fetch_add(
                static_cast<uint64_t>(recv_ms), std::memory_order_relaxed);
            profiling::g_current_counters->native_av_mux_write_wall_ms.fetch_add(
                static_cast<uint64_t>(mux_ms), std::memory_order_relaxed);
        }
    }
}

} // namespace chronon3d::cli

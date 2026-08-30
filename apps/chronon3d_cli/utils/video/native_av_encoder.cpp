#include "native_av_encoder.hpp"
#include "native_av_encoder_lifecycle.hpp"
#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/core/profiling/render_counter_types.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <mutex>
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/backends/vulkan/cuda_vulkan_surface_bridge.hpp>
#endif
#include <cuda.h>
#include <libavutil/hwcontext_cuda.h>
#endif

// Convenience: steady clock helpers
namespace {
using Clock = std::chrono::steady_clock;
inline double elapsed_ms(const Clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

bool set_codec_option_checked(AVCodecContext* codec, const char* key,
                              const std::string& value) {
    const int rc = av_opt_set(codec, key, value.c_str(), AV_OPT_SEARCH_CHILDREN);
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

NativeAvEncoder::NativeAvEncoder(
    std::shared_ptr<media::VideoDeviceRuntime> device_runtime)
    : device_runtime_(std::move(device_runtime)) {}

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

namespace {

bool validate_encoder_options(const FfmpegPipeOptions& options) {
    return options.width > 0 && options.height > 0 &&
           options.canonical_fps_num() > 0 && options.canonical_fps_den() > 0 &&
           !options.output_path.empty();
}

} // namespace

bool NativeAvEncoder::open(const FfmpegPipeOptions& options) {
    if (mux_ || codec_) {
        spdlog::error("[native_av] Encoder already open");
        return false;
    }

    open_complete_ = false;

    if (!validate_encoder_options(options)) {
        spdlog::error("[native_av] Invalid encoder options (w={}, h={}, fps={}, path='{}')",
                      options.width, options.height, options.fps, options.output_path);
        return false;
    }

    options_ = options;
    // Normalize the last external integer-only callers at the boundary. All
    // FFmpeg state below is then derived from the rational contract.
    options_.fps_num = options.canonical_fps_num();
    options_.fps_den = options.canonical_fps_den();
    frames_written_ = 0;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    frames_submitted_ = 0;
    cuda_pending_peak_ = 0;
    cuda_backpressure_wait_count_ = 0;
    pending_cuda_frames_.clear();
    reusable_cuda_frames_.clear();
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
    encoder_hwframe_get_buffer_ms_ = 0.0;
    encoder_surface_acquire_ms_    = 0.0;
    encoder_nvenc_submit_ms_       = 0.0;
    encoder_queue_backpressure_wait_ms_ = 0.0;
    encoder_packet_drain_ms_       = 0.0;
    direct_yuv_cuda_launch_ms_     = 0.0;
    direct_yuv_cuda_wait_ms_       = 0.0;

    const std::string filename = options_.output_path;

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
    codec_->time_base = AVRational{options_.fps_den, options_.fps_num};
    codec_->framerate = AVRational{options_.fps_num, options_.fps_den};
    // GOP size = 1 second (fps frames) for reliable decoder compatibility.
    // With fps*2, the very subtle tracking_breathing animation (4% scale over
    // 120 frames) produces P-frames with 99.7% skip on the "ultrafast" preset
    // — some H.264 decoders can't decode those near-empty P-frames, resulting
    // in visible frames only at keyframe intervals.  fps*2 worked for most
    // content but broke for near-static scenes with tiny per-frame changes.
    codec_->gop_size  = std::max(1, static_cast<int>(std::llround(
        static_cast<double>(options_.fps_num) / options_.fps_den)));
    codec_->max_b_frames = 0;              // no B-frames for lowest latency

    // NVENC consumes CUDA frames. The GPU path is enabled only for the
    // native NVENC profile; all other encoders retain the CPU YUV contract.
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    gpu_nvenc_ = options_.hardware_encoder == "nvenc";
    if (gpu_nvenc_) {
        const auto hw_t0 = Clock::now();
        if (!device_runtime_) {
            spdlog::error("[native_av] NVENC requires a video device runtime (none provided)");
            return false;
        }
        // Borrow the persistent hwdevice owned by the runtime. No cuInit,
        // no device discovery, no primary-context retain here: the runtime
        // did all of it once for the whole engine lifetime.
        cuda_device_ref_ = device_runtime_->ref_cuda_hwdevice();
        if (!cuda_device_ref_) {
            spdlog::error("[native_av] failed to borrow CUDA hwdevice from the video device runtime");
            return false;
        }
        auto* av_device = reinterpret_cast<AVHWDeviceContext*>(cuda_device_ref_->data);
        auto* av_cuda = av_device
            ? reinterpret_cast<AVCUDADeviceContext*>(av_device->hwctx)
            : nullptr;
        auto gpu = device_runtime_->gpu();
        spdlog::info("[native_av] CUDA context contract: native={} ffmpeg={}",
                     gpu ? reinterpret_cast<void*>(gpu->native_context_handle()) : nullptr,
                     av_cuda ? static_cast<void*>(av_cuda->cuda_ctx) : nullptr);
        if (!av_cuda || !av_cuda->cuda_ctx) return false;
        // FAIL_CLOSED: the FFmpeg hwdevice must alias the exact primary CUDA
        // context retained by the runtime. A second context would make the
        // device pointers invalid to the CUDA kernels and to AVHWFrames.
        if (!device_runtime_->context_matches(
                reinterpret_cast<std::uintptr_t>(av_cuda->cuda_ctx))) {
            spdlog::error("[native_av] FAIL_CLOSED: FFmpeg CUDA context {} does not match "
                          "GpuRuntime primary context {}",
                          static_cast<void*>(av_cuda->cuda_ctx),
                          gpu ? reinterpret_cast<void*>(gpu->native_context_handle()) : nullptr);
            return false;
        }
        if (cuda_stream_) {
            (void)cuStreamDestroy(cuda_stream_);
            cuda_stream_ = nullptr;
        }
        cuda_context_ = av_cuda->cuda_ctx;
        if (cuCtxSetCurrent(reinterpret_cast<CUcontext>(cuda_context_)) != CUDA_SUCCESS) {
            spdlog::error("[native_av] FFmpeg CUDA context activation failed");
            return false;
        }
        if (cuStreamCreate(&cuda_stream_, CU_STREAM_NON_BLOCKING) != CUDA_SUCCESS) {
            spdlog::error("[native_av] FFmpeg CUDA stream creation failed");
            return false;
        }
        std::string frames_reason;
        cuda_frames_ref_ = device_runtime_->ref_cuda_frames(
            static_cast<std::uint32_t>(options_.width),
            static_cast<std::uint32_t>(options_.height),
            AV_PIX_FMT_NV12, frames_reason);
        if (!cuda_frames_ref_) {
            spdlog::error("[native_av] persistent CUDA frames context unavailable: {}",
                          frames_reason);
            return false;
        }
        open_hw_ctx_ms_ = elapsed_ms(hw_t0);
        if (!options_.direct_yuv_mode) {
#ifdef CHRONON3D_ENABLE_VULKAN
            const auto warmup_t0 = Clock::now();
            try {
                backends::vulkan::CudaNv12SurfaceCompositor::warm_up(
                    reinterpret_cast<CUcontext>(cuda_context_));
            } catch (const std::exception& error) {
                spdlog::error("[native_av] CUDA compositor warm-up failed before NVENC: {}",
                              error.what());
                return false;
            }
            cuda_compositor_warmup_ms_ = elapsed_ms(warmup_t0);
#else
            spdlog::error("[native_av] FullGraph NVENC requires Vulkan interop");
            return false;
#endif
        } else {
            spdlog::info("[direct-yuv] skipped Vulkan compositor warm-up");
        }
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
        // NVENC does not expose x264's CRF option.  Keep this guard based on
        // the resolved encoder name as well as gpu_nvenc_: builds without
        // CUDA interop can still select h264_nvenc and must not send it crf.
        const std::string encoder_name = resolve_encoder_name(options_);
        const bool is_nvenc = encoder_name == "h264_nvenc" ||
                              encoder_name == "hevc_nvenc";
        if (!gpu_nvenc_ && !is_nvenc &&
            !set_codec_option_checked(codec_, "crf", crf_str)) return false;
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


    // 4. Open the codec
    const auto nvenc_t0 = Clock::now();
    if (avcodec_open2(codec_, encoder, nullptr) < 0) {
        spdlog::error("[native_av] avcodec_open2 failed");
        return false;
    }
    open_nvenc_ms_ = elapsed_ms(nvenc_t0);

    // MuxSession owns all libavformat state and container I/O.
    mux_ = std::make_unique<chronon3d::media::MuxSession>();
    std::string mux_reason;
    if (!mux_->open(filename, *codec_, mux_reason)) {
        spdlog::error("[native_av] mux session open failed: {}", mux_reason);
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
    spdlog::info("[native_av] close: draining CUDA pending={}", pending_cuda_frames_.size());
    if (gpu_nvenc_ && !drain_ready_cuda_frames(true)) {
        spdlog::error("[native_av] failed to drain pending CUDA frames");
    }
    spdlog::info("[native_av] close: CUDA drain complete");
#endif

    // 1. Drain encoder: send NULL to flush internal buffers (encoder flush).
    const auto t_flush0 = Clock::now();
    spdlog::info("[native_av] close: sending encoder flush");
    avcodec_send_frame(codec_, nullptr);
    spdlog::info("[native_av] close: encoder flush sent");

    // 2. Receive and write remaining packets (timing tracked inside drain_packets)
    drain_packets();
    spdlog::info("[native_av] close: packets drained");
    const double flush_ms = elapsed_ms(t_flush0);
    native_flush_ms_ += flush_ms;

    // 3. Write trailer (finalizes the MP4 file, writes moov atom, etc.)
    //    This is the mux finalization, measured separately from the encoder
    //    flush so the two tails never mask each other.
    const auto t_trailer0 = Clock::now();
    if (mux_) {
        spdlog::info("[native_av] close: finalizing mux session");
        (void)mux_->finalize();
        spdlog::info("[native_av] close: mux session finalized");
    }
    const double trailer_ms = elapsed_ms(t_trailer0);

    native_trailer_ms_ += trailer_ms;

    if (profiling::g_current_counters) {
        profiling::g_current_counters->encoder_flush_wall_ms.fetch_add(
            static_cast<uint64_t>(flush_ms), std::memory_order_relaxed);
        profiling::g_current_counters->mux_finalize_wall_ms.fetch_add(
            static_cast<uint64_t>(trailer_ms), std::memory_order_relaxed);
    }


    // Codec teardown remains encoder-owned; container teardown is owned by
    // MuxSession and happens after the trailer has been written.

    spdlog::info("[native_av] Closed native encoder — {} frames written, YUV cache: {} hits / {} misses",
                 frames_written_, cache_hits_, cache_misses_);
    return true;
}

NativeAvEncoder::~NativeAvEncoder() {
    shutdown_noexcept();
}

void NativeAvEncoder::shutdown_noexcept() noexcept {
    // close() performs the codec drain/trailer only while the encoder is
    // fully open. Any exception or failed open must use the abort path.
    if (open_complete_) {
        try {
            (void)close();
        } catch (...) {
            spdlog::error("[native_av] exception during destructor close; forcing abort cleanup");
            abort_open();
        }
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
    for (auto* reusable : reusable_cuda_frames_) {
        av_frame_free(&reusable);
    }
    reusable_cuda_frames_.clear();
    if (cuda_context_) (void)cuCtxSetCurrent(reinterpret_cast<CUcontext>(cuda_context_));
#ifdef CHRONON3D_ENABLE_VULKAN
    cuda_nv12_compositors_.clear();
#endif
    if (cuda_stream_) {
        (void)cuStreamDestroy(cuda_stream_);
        cuda_stream_ = nullptr;
    }
#endif
    // Release frame/packet wrappers and codec state. MuxSession owns all
    // libavformat state and closes its container independently.
    av_packet_free(&packet_);
    av_frame_free(&frame_);
    avcodec_free_context(&codec_);
    mux_.reset();
    packet_ = nullptr;
    frame_ = nullptr;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    av_buffer_unref(&cuda_frames_ref_);
    av_buffer_unref(&cuda_device_ref_);
    // The CUDA context + hwdevice belong to the device runtime; only the
    // borrowed pointers are cleared here.
    cuda_context_ = nullptr;
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
            const CUresult sync_result = cuEventSynchronize(pending.ready);
            if (sync_result != CUDA_SUCCESS) {
                const char* name = nullptr;
                const char* text = nullptr;
                cuGetErrorName(sync_result, &name);
                cuGetErrorString(sync_result, &text);
                spdlog::error("[native_av] CUDA encode event wait failed: {} ({})",
                              name ? name : "unknown", text ? text : "unknown");
                return false;
            }
            const auto wait_ms = elapsed_ms(wait_t0);
            native_backpressure_ms_ += wait_ms;
            encoder_queue_backpressure_wait_ms_ += wait_ms;
            ++cuda_backpressure_wait_count_;
            if (counters_) {
                counters_->cuda_encode_event_wait_count.fetch_add(1, std::memory_order_relaxed);
                counters_->cuda_encode_event_wait_us.fetch_add(
                    static_cast<std::uint64_t>(wait_ms * 1000.0), std::memory_order_relaxed);
            }
        } else if (ready != CUDA_SUCCESS) {
            const char* name = nullptr;
            const char* text = nullptr;
            cuGetErrorName(ready, &name);
            cuGetErrorString(ready, &text);
            spdlog::error("[native_av] CUDA encode event query failed: {} ({})",
                          name ? name : "unknown", text ? text : "unknown");
            return false;
        }

        const auto send_t0 = Clock::now();
        const int send_ret = avcodec_send_frame(codec_, pending.frame);
        const double send_dur = elapsed_ms(send_t0);
        native_send_frame_ms_ += send_dur;
        encoder_nvenc_submit_ms_ += send_dur;
        av_frame_unref(pending.frame);
        reusable_cuda_frames_.push_back(pending.frame);
        pending.frame = nullptr;
        cuEventDestroy(pending.ready);
        pending.ready = nullptr;
        pending_cuda_frames_.pop_front();
        if (send_ret < 0) {
            char error[AV_ERROR_MAX_STRING_SIZE]{};
            av_strerror(send_ret, error, sizeof(error));
            spdlog::error("[native_av] avcodec_send_frame failed for GPU frame {}: {}",
                          frames_written_, error);
            return false;
        }
        const auto drain_t0 = Clock::now();
        if (!drain_packets()) {
            spdlog::error("[native_av] NVENC packet drain failed after GPU frame {}",
                          frames_written_);
            return false;
        }
        encoder_packet_drain_ms_ += elapsed_ms(drain_t0);
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
    return write_native_surface_impl(backend, source, destination, false);
}

bool NativeAvEncoder::write_prepared_native_surface(
    graph::RenderBackend& backend,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle destination) {
    return write_native_surface_impl(backend, source, destination, true);
}

bool NativeAvEncoder::write_direct_yuv(const DirectYuvFrame& direct) {
#ifndef CHRONON3D_ENABLE_CUDA_INTEROP
    (void)direct;
    return false;
#else
    const auto direct_submit_t0 = Clock::now();
    last_frame_telemetry_ = EncoderFrameTelemetry{};
    if (!gpu_nvenc_ || !cuda_context_ || !cuda_frames_ref_ ||
        !direct.decoded || direct.decoded->format != AV_PIX_FMT_CUDA ||
        !direct.decoded->data[0] || !direct.decoded->data[1] ||
        !direct.program || direct.program->batch.empty() ||
        direct.program->resources.empty()) {
        spdlog::error("[native_av] invalid DirectCudaYuv frame contract");
        return false;
    }
    if (!direct_yuv_compositor_) {
        try {
            direct_yuv_compositor_ =
                std::make_unique<media::CudaDirectNv12Compositor>(
                    reinterpret_cast<CUcontext>(cuda_context_));
        } catch (const std::exception& error) {
            spdlog::error("[native_av] DirectCudaYuv compositor init failed: {}",
                          error.what());
            return false;
        }
    }
    if (!drain_ready_cuda_frames(false)) return false;
    if (pending_cuda_frames_.size() >= kCudaEncodeRingSlots &&
        !drain_ready_cuda_frames(true)) {
        ++cuda_backpressure_wait_count_;
        return false;
    }

    const auto acq_t0 = Clock::now();
    AVFrame* gpu_frame = nullptr;
    if (!reusable_cuda_frames_.empty()) {
        gpu_frame = reusable_cuda_frames_.front();
        reusable_cuda_frames_.pop_front();
        av_frame_unref(gpu_frame);
    } else {
        gpu_frame = av_frame_alloc();
    }
    encoder_surface_acquire_ms_ += elapsed_ms(acq_t0);
    if (!gpu_frame) return false;
    gpu_frame->format = AV_PIX_FMT_CUDA;
    gpu_frame->width = options_.width;
    gpu_frame->height = options_.height;
    gpu_frame->hw_frames_ctx = av_buffer_ref(cuda_frames_ref_);
    const auto hw_t0 = Clock::now();
    const int hw_rc = av_hwframe_get_buffer(cuda_frames_ref_, gpu_frame, 0);
    encoder_hwframe_get_buffer_ms_ += elapsed_ms(hw_t0);
    if (hw_rc < 0 || !gpu_frame->data[0] || !gpu_frame->data[1]) {
        av_frame_unref(gpu_frame);
        reusable_cuda_frames_.push_back(gpu_frame);
        return false;
    }

    auto* frames = direct.decoded->hw_frames_ctx
        ? reinterpret_cast<AVHWFramesContext*>(direct.decoded->hw_frames_ctx->data)
        : nullptr;
    if (!frames || frames->sw_format != AV_PIX_FMT_NV12) {
        spdlog::error("[native_av] DirectCudaYuv currently requires NV12 input");
        av_frame_unref(gpu_frame);
        reusable_cuda_frames_.push_back(gpu_frame);
        return false;
    }
    const auto launch_t0 = Clock::now();
    const bool launch_ok = direct_yuv_compositor_->composite_direct_nv12_batch(
            direct.program->batch, direct.program->resources,
            reinterpret_cast<CUdeviceptr>(direct.decoded->data[0]),
            direct.decoded->linesize[0],
            reinterpret_cast<CUdeviceptr>(direct.decoded->data[1]),
            direct.decoded->linesize[1],
            reinterpret_cast<CUdeviceptr>(gpu_frame->data[0]),
            gpu_frame->linesize[0],
            reinterpret_cast<CUdeviceptr>(gpu_frame->data[1]),
            gpu_frame->linesize[1],
            static_cast<std::uint32_t>(options_.width),
            static_cast<std::uint32_t>(options_.height), cuda_stream_);
    direct_yuv_cuda_launch_ms_ += elapsed_ms(launch_t0);
    if (!launch_ok) {
        spdlog::error("[native_av] DirectCudaYuv kernel dispatch failed");
        av_frame_unref(gpu_frame);
        reusable_cuda_frames_.push_back(gpu_frame);
        return false;
    }
    gpu_frame->pts = static_cast<int64_t>(frames_submitted_++);
    CUevent ready = nullptr;
    if (cuEventCreate(&ready, CU_EVENT_DISABLE_TIMING) != CUDA_SUCCESS ||
        cuEventRecord(ready, cuda_stream_) != CUDA_SUCCESS) {
        if (ready) cuEventDestroy(ready);
        av_frame_unref(gpu_frame);
        reusable_cuda_frames_.push_back(gpu_frame);
        return false;
    }
    // NVDEC owns a finite pool of input surfaces. Complete the direct-YUV
    // kernel before releasing the source AVFrame; retaining all input frames
    // until the encoder consumes the outputs exhausts the decoder pool and
    // regresses the render loop. The output remains queued for NVENC.
    const auto sync_t0 = Clock::now();
    const CUresult sync_res = cuEventSynchronize(ready);
    direct_yuv_cuda_wait_ms_ += elapsed_ms(sync_t0);
    if (sync_res != CUDA_SUCCESS) {
        cuEventDestroy(ready);
        av_frame_unref(gpu_frame);
        reusable_cuda_frames_.push_back(gpu_frame);
        return false;
    }
    pending_cuda_frames_.push_back(
        PendingCudaFrame{gpu_frame, ready,
                         runtime::kInvalidRenderSurfaceHandle,
                         direct.program,
                         chronon3d::media::HwFrameRef(direct.decoded)});
    cuda_pending_peak_ = std::max<std::uint64_t>(
        cuda_pending_peak_, pending_cuda_frames_.size());
    if (counters_) {
        auto observed = counters_->cuda_encode_queue_peak.load(std::memory_order_relaxed);
        const auto current = static_cast<std::uint64_t>(pending_cuda_frames_.size());
        while (observed < current &&
               !counters_->cuda_encode_queue_peak.compare_exchange_weak(
                   observed, current, std::memory_order_relaxed)) {}
    }
    const bool drained = drain_ready_cuda_frames(false);
    last_frame_telemetry_.encoder_ms = elapsed_ms(direct_submit_t0);
    last_frame_telemetry_.frame_submit_ms = last_frame_telemetry_.encoder_ms;
    return drained;
#endif
}

bool NativeAvEncoder::write_native_surface_impl(
    graph::RenderBackend& backend,
    runtime::RenderSurfaceHandle source,
    runtime::RenderSurfaceHandle destination,
    bool surface_already_prepared) {
#if !defined(CHRONON3D_ENABLE_CUDA_INTEROP) || !defined(CHRONON3D_ENABLE_VULKAN)
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
    if (!vulkan->cuda_context_matches_device(
            reinterpret_cast<CUcontext>(cuda_context_))) {
        spdlog::error("[native_av] Vulkan/CUDA physical-device UUID mismatch");
        return false;
    }
    if (!surface_already_prepared) {
        const auto copy_result = vulkan->copy_surface_to_cuda_encoder(source, destination, false);
        if (!copy_result.ok()) {
            spdlog::error("[native_av] GPU surface conversion failed: {}", copy_result.error().message);
            return false;
        }
    }
    try {
        if (!drain_ready_cuda_frames(false)) return false;
        if (pending_cuda_frames_.size() >= kCudaEncodeRingSlots &&
            !drain_ready_cuda_frames(true)) {
            spdlog::error("[native_av] CUDA encoder queue backpressure failed");
            return false;
        }

        // Initialize the Vulkan/CUDA compositor before asking FFmpeg for a
        // CUDA frame.  The compositor owns the external-memory module and
        // must be validated independently from AVHWFrames allocation; doing
        // this in the opposite order allowed a stale FFmpeg/primary-context
        // error to be reported as a PTX module-load failure.
        const auto key = static_cast<std::uint64_t>(destination);
        auto compositor_it = cuda_nv12_compositors_.find(key);
        if (compositor_it == cuda_nv12_compositors_.end()) {
            const auto info = vulkan->export_cuda_external_memory(destination);
            auto compositor = std::make_unique<backends::vulkan::CudaNv12SurfaceCompositor>(
                info, reinterpret_cast<CUcontext>(cuda_context_));
            compositor_it = cuda_nv12_compositors_.emplace(key, std::move(compositor)).first;
        }

        AVFrame* gpu_frame = nullptr;
        if (!reusable_cuda_frames_.empty()) {
            gpu_frame = reusable_cuda_frames_.front();
            reusable_cuda_frames_.pop_front();
            av_frame_unref(gpu_frame);
        } else {
            gpu_frame = av_frame_alloc();
        }
        if (!gpu_frame) return false;
        gpu_frame->format = AV_PIX_FMT_CUDA;
        gpu_frame->width = options_.width;
        gpu_frame->height = options_.height;
        gpu_frame->hw_frames_ctx = av_buffer_ref(cuda_frames_ref_);
        if (av_hwframe_get_buffer(cuda_frames_ref_, gpu_frame, 0) < 0) {
            spdlog::error("[native_av] av_hwframe_get_buffer failed for GPU frame {}", frames_written_);
            av_frame_unref(gpu_frame);
            reusable_cuda_frames_.push_back(gpu_frame);
            return false;
        }
        if (!gpu_frame->data[0] || !gpu_frame->data[1]) {
            spdlog::error("[native_av] NV12 CUDA frame has no Y/UV planes");
            av_frame_unref(gpu_frame);
            reusable_cuda_frames_.push_back(gpu_frame);
            return false;
        }
        if (!compositor_it->second->composite_surface_to_nv12(
                reinterpret_cast<CUdeviceptr>(gpu_frame->data[0]),
                gpu_frame->linesize[0],
                reinterpret_cast<CUdeviceptr>(gpu_frame->data[1]),
                gpu_frame->linesize[1],
                static_cast<std::uint32_t>(options_.width),
                static_cast<std::uint32_t>(options_.height), cuda_stream_)) {
            spdlog::error("[native_av] direct RGBA->NV12 CUDA compositor failed");
            av_frame_unref(gpu_frame);
            reusable_cuda_frames_.push_back(gpu_frame);
            return false;
        }
        if (counters_) {
            counters_->native_surface_yuv_frames.fetch_add(1, std::memory_order_relaxed);
            counters_->native_surface_yuv_bytes.fetch_add(
                static_cast<std::uint64_t>(options_.width) *
                    static_cast<std::uint64_t>(options_.height) * 3 / 2,
                std::memory_order_relaxed);
        }
        // The direct compositor signals CUDA completion on the imported
        // destination surface; publish that ownership transition to Vulkan.
        const auto prepared = vulkan->prepare_cuda_surface_for_vulkan(destination);
        if (!prepared.ok()) {
            spdlog::error("[native_av] failed to publish CUDA completion for surface {}: {}",
                          destination, prepared.error().message);
            av_frame_unref(gpu_frame);
            reusable_cuda_frames_.push_back(gpu_frame);
            return false;
        }
        CUevent ready = nullptr;
        if (cuEventCreate(&ready, CU_EVENT_DISABLE_TIMING) != CUDA_SUCCESS ||
            cuEventRecord(ready, cuda_stream_) != CUDA_SUCCESS) {
            if (ready) cuEventDestroy(ready);
            spdlog::error("[native_av] CUDA completion event failed for GPU frame {}",
                          frames_submitted_);
            av_frame_unref(gpu_frame);
            reusable_cuda_frames_.push_back(gpu_frame);
            return false;
        }
        gpu_frame->pts = static_cast<int64_t>(frames_submitted_++);
        pending_cuda_frames_.push_back(PendingCudaFrame{gpu_frame, ready, destination});
        cuda_pending_peak_ = std::max<std::uint64_t>(
            cuda_pending_peak_, pending_cuda_frames_.size());
        if (counters_) {
            auto observed = counters_->cuda_encode_queue_peak.load(std::memory_order_relaxed);
            const auto current = static_cast<std::uint64_t>(pending_cuda_frames_.size());
            while (observed < current &&
                   !counters_->cuda_encode_queue_peak.compare_exchange_weak(
                       observed, current, std::memory_order_relaxed)) {}
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

bool NativeAvEncoder::finish_native_surface(
    graph::RenderBackend& backend,
    runtime::RenderSurfaceHandle destination) {
#ifndef CHRONON3D_ENABLE_CUDA_INTEROP
    (void)backend; (void)destination;
    return true;
#else
    (void)backend;
    spdlog::info("[native_av] finish_native_surface destination={} pending={}",
                 destination, pending_cuda_frames_.size());
    // Pending CUDA work is ordered on one stream. Drain in submission order
    // until the event belonging to this surface has been consumed. This is
    // the ownership hand-off that makes FrameInteropRing::release safe.
    for (;;) {
        auto it = std::find_if(
            pending_cuda_frames_.begin(), pending_cuda_frames_.end(),
            [destination](const PendingCudaFrame& pending) {
                return pending.surface == destination;
            });
        if (it == pending_cuda_frames_.end()) return true;
        if (!drain_ready_cuda_frames(true)) return false;
    }
#endif
}

bool NativeAvEncoder::poll_native_surface(
    graph::RenderBackend& backend,
    runtime::RenderSurfaceHandle destination) {
#ifndef CHRONON3D_ENABLE_CUDA_INTEROP
    (void)backend; (void)destination;
    return true;
#else
    (void)backend;
    if (!drain_ready_cuda_frames(false)) return false;
    const auto it = std::find_if(
        pending_cuda_frames_.begin(), pending_cuda_frames_.end(),
        [destination](const PendingCudaFrame& pending) {
            return pending.surface == destination;
        });
    if (it == pending_cuda_frames_.end()) return true;
    return cuEventQuery(it->ready) == CUDA_SUCCESS;
#endif
}

} // namespace chronon3d::cli

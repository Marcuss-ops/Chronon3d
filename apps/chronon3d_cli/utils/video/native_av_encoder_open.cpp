#include "native_av_encoder.hpp"
#include "native_av_encoder_internal.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#include <libavutil/hwcontext_cuda.h>
#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/backends/vulkan/cuda_nv12_surface_compositor.hpp>
#endif
#endif

namespace chronon3d::cli {
using Clock = detail::NativeAvClock;
using detail::elapsed_ms;

namespace {

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

bool validate_encoder_options(const FfmpegPipeOptions& options) {
    return options.width > 0 && options.height > 0 &&
           options.canonical_fps_num() > 0 && options.canonical_fps_den() > 0 &&
           !options.output_path.empty();
}

} // namespace

NativeAvEncoder::NativeAvEncoder(
    std::shared_ptr<media::VideoDeviceRuntime> device_runtime)
    : device_runtime_(std::move(device_runtime)) {}

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

    last_converted_digest_       = 0;
    last_converted_width_        = 0;
    last_converted_height_       = 0;
    last_converted_pix_fmt_      = -1;
    last_converted_apply_gamma_  = false;
    last_converted_color_matrix_ = -1;
    cache_hits_   = 0;
    cache_misses_ = 0;

    native_convert_ms_ = 0.0;
    native_send_frame_ms_ = 0.0;
    native_backpressure_ms_ = 0.0;
    native_flush_ms_ = 0.0;
    native_receive_packet_ms_ = 0.0;
    native_mux_write_ms_ = 0.0;
    native_trailer_ms_ = 0.0;
    encoder_hwframe_get_buffer_ms_ = 0.0;
    encoder_surface_acquire_ms_ = 0.0;
    encoder_nvenc_submit_ms_ = 0.0;
    encoder_queue_backpressure_wait_ms_ = 0.0;
    encoder_packet_drain_ms_ = 0.0;
    direct_yuv_cuda_launch_ms_ = 0.0;
    direct_yuv_cuda_wait_ms_ = 0.0;
    // Applied/requested-settings telemetry: cleared on every open (a pooled
    // encoder instance may be re-opened with a different backend, and early
    // failure paths must not leak stale values into the closeout report).
    applied_encoder_preset_.clear();
    applied_encoder_rate_control_.clear();
    applied_encoder_async_depth_ = 0;
    requested_encoder_rate_control_.clear();
    requested_encoder_preset_.clear();
    resolved_encoder_config_.reset();

    // ── C-1/C-2: resolve + validate the encoder configuration BEFORE any
    // FFmpeg/decoder/CUDA/render setup. NativeAvEncoder applies the resolved
    // configuration verbatim; it never interprets raw rate-control/preset/
    // tune strings into policy itself (the single authority is
    // resolve_encoder_config). Unknown rate-control strings, unsupported
    // combinations (NVENC + explicit crf, NVENC + tune) and invalid presets
    // fail here — fast, with a precise message.
    auto config_request = make_encoder_config_request(options_);
    auto resolution = resolve_encoder_config(config_request);
    if (!resolution) {
        spdlog::error("[native_av] Invalid encoder configuration: {}",
                      resolution.error().message);
        return false;
    }
	// ResolvedEncoderConfig is intentionally immutable after construction and
	// therefore deletes assignment operators. Construct the optional in place
	// instead of attempting move-assignment into an existing optional.
	resolved_encoder_config_.emplace(std::move(resolution).value());
    const auto& resolved = *resolved_encoder_config_;

    gpu_nvenc_ = resolved.backend() == EncoderBackend::Nvenc;
#ifndef CHRONON3D_ENABLE_CUDA_INTEROP
    if (gpu_nvenc_) {
        spdlog::error("[native_av] NVENC requires a CUDA-interop build (CHRONON3D_ENABLE_CUDA_INTEROP)");
        return false;
    }
#endif

    // Requested (pre-resolution) intent, for telemetry. "engine-default"
    // means the caller never asked — distinct from an explicit request.
    requested_encoder_rate_control_ =
        options_.rate_control_mode_explicit ? options_.rate_control_mode
                                            : "engine-default";
    requested_encoder_preset_ =
        options_.preset_explicit ? options_.preset : "engine-default";

    const std::string filename = options_.output_path;

    const AVCodec* encoder =
        avcodec_find_encoder_by_name(resolved.encoder_name().c_str());
    if (!encoder) {
        spdlog::error("[native_av] avcodec_find_encoder_by_name('{}') failed",
                      resolved.encoder_name());
        return false;
    }

    codec_ = avcodec_alloc_context3(encoder);
    if (!codec_) {
        spdlog::error("[native_av] avcodec_alloc_context3 failed");
        return false;
    }

    codec_->width = options_.width;
    codec_->height = options_.height;
    codec_->time_base = AVRational{options_.fps_den, options_.fps_num};
    codec_->framerate = AVRational{options_.fps_num, options_.fps_den};
    codec_->gop_size = std::max(1, static_cast<int>(std::llround(
        static_cast<double>(options_.fps_num) / options_.fps_den)));
    codec_->max_b_frames = 0;

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    if (gpu_nvenc_) {
        const auto hw_t0 = Clock::now();
        if (!device_runtime_) {
            spdlog::error("[native_av] NVENC requires a video device runtime (none provided)");
            return false;
        }
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

    // ── Apply the RESOLVED configuration verbatim ────────────────────────
    // Everything below is driven by `resolved` (from
    // resolve_encoder_config). NativeAvEncoder no longer interprets
    // rate-control/preset/tune strings: the resolver already validated the
    // combination for this backend and produced the exact option set.
    {
        const std::string& preset = resolved.preset();
        if (!preset.empty() &&
            !set_codec_option_checked(codec_, "preset", preset)) return false;
    }
    if (const auto crf = resolved.crf()) {
        char crf_str[16];
        snprintf(crf_str, sizeof(crf_str), "%d", *crf);
        if (!set_codec_option_checked(codec_, "crf", crf_str)) return false;
    }
    if (resolved.bitrate()) codec_->bit_rate = *resolved.bitrate();
    const std::string& codec_name = resolved.encoder_name();
    if (codec_name == "libx264" || codec_name == "libx264rgb") {
        if (options_.encode_threads > 0) {
            char threads_str[16];
            snprintf(threads_str, sizeof(threads_str), "%d", options_.encode_threads);
            if (!set_codec_option_checked(codec_, "threads", threads_str)) return false;
        } else if (!set_codec_option_checked(codec_, "threads", "auto")) {
            return false;
        }
        if (!set_codec_option_checked(codec_, "thread_type", "frame")) return false;
    }
    if (const auto tune = resolved.tune()) {
        // The resolver only ever emits a tune on software (libx264-family)
        // backends; NVENC + tune fails during resolution.
        if (!set_codec_option_checked(codec_, "tune", *tune)) return false;
    }

    // ── Native NVENC pipeline depth + resolved rate control ─────────────
    // The NVENC wrapper in this FFmpeg build (libavcodec 58 / 4.2-4.4) does
    // NOT expose an `async_depth` AVOption on h264_nvenc/hevc_nvenc — the
    // wrapper's in-flight queue is bounded by its surface pool ("surfaces",
    // default 4). With only 4 surfaces every avcodec_send_frame blocks
    // whenever the driver still holds all of them, serializing
    // decode/composite behind encode (certification measured the submit
    // wall dominating the render loop). We therefore map the resolved
    // engine-level in-flight depth onto a surface pool sized depth + 4 so
    // the submit path never starves while `depth` frames are in flight.
    if (gpu_nvenc_) {
        const int async_depth = resolved.async_depth();
        // surfaces option range is [0, 64]; keep headroom inside the range.
        const int nvenc_surfaces = std::min(60, async_depth + 4);
        char surf_str[16];
        snprintf(surf_str, sizeof(surf_str), "%d", nvenc_surfaces);
        if (!set_codec_option_checked(codec_, "surfaces", surf_str)) {
            return false;
        }
        switch (resolved.rate_control()) {
            case ResolvedEncoderRateControl::ConstantQp: {
                if (!set_codec_option_checked(codec_, "rc", "constqp")) {
                    return false;
                }
                char qp_str[16];
                snprintf(qp_str, sizeof(qp_str), "%d", resolved.qp().value_or(-1));
                if (!set_codec_option_checked(codec_, "qp", qp_str)) {
                    return false;
                }
                break;
            }
            case ResolvedEncoderRateControl::Vbr:
                if (!set_codec_option_checked(codec_, "rc", "vbr")) {
                    return false;
                }
                break;
            case ResolvedEncoderRateControl::Cbr:
                if (!set_codec_option_checked(codec_, "rc", "cbr")) {
                    return false;
                }
                break;
            case ResolvedEncoderRateControl::DriverDefault:
                // Explicit engine decision: no rc option — the NVENC driver
                // default applies. Kept visible in telemetry as
                // "driver-default" and rejected by validate_for_benchmark.
                break;
            case ResolvedEncoderRateControl::ConstantQuality:
                // Unreachable: the resolver rejects NVENC + explicit crf.
                spdlog::error("[native_av] Internal error: constant-quality NVENC "
                              "reached open() despite resolution rejection");
                return false;
        }
        if (resolved.bitrate()) codec_->bit_rate = *resolved.bitrate();
        applied_encoder_preset_ = resolved.preset_label();
        applied_encoder_rate_control_ = resolved.rate_control_label();
        applied_encoder_async_depth_ = async_depth;
        spdlog::info(
            "[native_av] NVENC tuning applied: rc={} preset={} async_depth={} "
            "nvenc_surfaces={} qp={} bitrate={}",
            resolved.rate_control_label(), resolved.preset_label(),
            async_depth, nvenc_surfaces,
            resolved.qp().value_or(-1),
            static_cast<long long>(resolved.bitrate().value_or(0)));
    } else {
        // Software encoder: resolved settings also surface in telemetry.
        applied_encoder_preset_ = resolved.preset_label();
        applied_encoder_rate_control_ = resolved.rate_control_label();
        applied_encoder_async_depth_ = 0;
    }

    const auto nvenc_t0 = Clock::now();
    if (avcodec_open2(codec_, encoder, nullptr) < 0) {
        spdlog::error("[native_av] avcodec_open2 failed");
        return false;
    }
    open_nvenc_ms_ = elapsed_ms(nvenc_t0);

    std::optional<chronon3d::media::AudioStreamConfig> audio_config;
    if (!options_.audio_source_path.empty()) {
        if (avformat_open_input(&audio_input_, options_.audio_source_path.c_str(),
                                nullptr, nullptr) < 0 ||
            avformat_find_stream_info(audio_input_, nullptr) < 0) {
            spdlog::error("[native_av] audio source open failed: {}", options_.audio_source_path);
            return false;
        }
        audio_input_stream_ = av_find_best_stream(
            audio_input_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (audio_input_stream_ < 0) {
            spdlog::error("[native_av] audio source has no audio stream: {}", options_.audio_source_path);
            return false;
        }
        audio_config = chronon3d::media::AudioStreamConfig{
            .params = audio_input_->streams[audio_input_stream_]->codecpar,
            .time_base = audio_input_->streams[audio_input_stream_]->time_base};
    }

    mux_ = std::make_unique<chronon3d::media::MuxSession>();
    std::string mux_reason;
    if (!mux_->open(chronon3d::media::MuxOpenConfig{
            .output_path = filename,
            .video_codec = codec_,
            .audio = audio_config}, mux_reason)) {
        spdlog::error("[native_av] mux session open failed: {}", mux_reason);
        return false;
    }

    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!frame_ || !packet_) {
        spdlog::error("[native_av] av_frame_alloc or av_packet_alloc failed");
        return false;
    }

    frame_->format = codec_->pix_fmt;
    frame_->width = codec_->width;
    frame_->height = codec_->height;
    if (!gpu_nvenc_ && av_frame_get_buffer(frame_, 32) < 0) {
        spdlog::error("[native_av] av_frame_get_buffer failed");
        return false;
    }

    spdlog::info(
        "[native_av] Opened native encoder: {}x{} @ {}fps, codec={}, rc={}, preset={}, output='{}'",
        options_.width, options_.height, options_.fps,
        resolved.encoder_name(), resolved.rate_control_label(),
        resolved.preset_label(), filename);
    open_complete_ = true;
    return true;
}

} // namespace chronon3d::cli

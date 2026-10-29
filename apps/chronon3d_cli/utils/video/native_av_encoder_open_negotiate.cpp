#include "native_av_encoder.hpp"
#include "native_av_encoder_internal.hpp"
#include "ffmpeg_profile_constants.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/media/video/keyframe_cadence.hpp>
#include <fmt/format.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/mathematics.h>
#include <libavcodec/version.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#include <libavutil/hwcontext_cuda.h>
#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/media/video/compositor/cuda/cuda_nv12_compositor.hpp>
#endif
#endif

namespace chronon3d::cli {
using Clock = detail::NativeAvClock;
using detail::elapsed_ms;

bool NativeAvEncoder::resolve_open_configuration() {
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
    return true;
}

bool NativeAvEncoder::configure_codec_and_surface() {
    const auto& resolved = *resolved_encoder_config_;

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
    // Newly generated SDR H.264 uses High; 10-bit PQ/HLG uses High 10. A
    // borrowed splice target is the exception: reproduce its declared profile
    // exactly or the packet splice gate rejects the mixed stream.
    if (encoder->id == AV_CODEC_ID_H264) {
        const int declared_profile = options_.borrowed_declared_stream
            ? options_.borrowed_declared_stream->profile
            : -1;
        const bool hdr_output = options_.color_transform.output == color::ColorSpace::PQ ||
                                options_.color_transform.output == color::ColorSpace::HLG;
        codec_->profile = declared_profile >= 0
            ? declared_profile
            : (hdr_output ? ffmpeg_profile::h264_high_10
                          : ffmpeg_profile::h264_high);
        // FFmpeg's NVENC wrapper can leave its private profile at Main even
        // when AVCodecContext::profile is set before avcodec_open2(). Pass the
        // profile through the encoder option as well so the emitted SPS and
        // packet facts match the assembly contract (and any borrowed stream).
        const char* profile_option = nullptr;
        switch (codec_->profile) {
            case ffmpeg_profile::h264_baseline:
                profile_option = "baseline";
                break;
            case ffmpeg_profile::h264_main:
                profile_option = "main";
                break;
            case ffmpeg_profile::h264_high:
                profile_option = "high";
                break;
            case ffmpeg_profile::h264_high_10:
                profile_option = "high10";
                break;
            default:
                break;
        }
        if (profile_option &&
            !detail::set_codec_option_checked(codec_, "profile", profile_option)) {
            return false;
        }
    }
    codec_->time_base = AVRational{options_.fps_den, options_.fps_num};
    codec_->framerate = AVRational{options_.fps_num, options_.fps_den};
    // The GOP is the assembly-ready contract's 2-second cadence, resolved by
    // the ONE keyframe-cadence authority (48 frames at 24 fps). Deriving it
    // from the frame rate alone emitted one keyframe per second and the
    // output-facts gateway rejected every production render (GOP 24 != 48).
    codec_->gop_size = media::video::keyframe_interval_frames(
        options_.fps_num, options_.fps_den);
    codec_->max_b_frames = 0;
    // ── Self-describing output stream ────────────────────────────────────
    // The container must describe the stream it carries, and this encoder must
    // be able to state its own parameter sets (profile/level/SPS-PPS) rather
    // than repeating them in-band on every keyframe. Both halves of the splice
    // lane depend on it: the borrowed container is declared from the source and
    // the encoder is certified to reproduce exactly that stream (P0-5E), and
    // the segment/oracle mechanism refuses a segment whose codec parameters do
    // not carry the same configuration record.
    codec_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    if (gpu_nvenc_) {
        if (options_.color_transform.output == color::ColorSpace::PQ ||
            options_.color_transform.output == color::ColorSpace::HLG) {
            spdlog::error(
                "[native_av] PQ/HLG output requires the CPU 10-bit encoder path; "
                "the CUDA surface path is not enabled for this export");
            return false;
        }
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
        const AVPixelFormat native_yuv_format =
            options_.direct_yuv_mode && options_.input_format == PipePixelFormat::P010
                ? AV_PIX_FMT_P010 : AV_PIX_FMT_NV12;
        cuda_frames_ref_ = device_runtime_->ref_cuda_frames(
            static_cast<std::uint32_t>(options_.width),
            static_cast<std::uint32_t>(options_.height),
            native_yuv_format, frames_reason);
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
                media::video::CudaNv12Compositor::warm_up(
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
        const bool hdr_output = options_.color_transform.output == color::ColorSpace::PQ ||
                                options_.color_transform.output == color::ColorSpace::HLG;
        const AVPixelFormat target_format = hdr_output
            ? AV_PIX_FMT_YUV420P10LE : AV_PIX_FMT_YUV420P;
        bool supported = encoder->pix_fmts == nullptr;
        if (encoder->pix_fmts) {
            for (const AVPixelFormat* format = encoder->pix_fmts;
                 *format != AV_PIX_FMT_NONE; ++format) {
                if (*format == target_format) { supported = true; break; }
            }
        }
        if (!supported) {
            spdlog::error(
                "[native_av] encoder '{}' does not support the pixel format required for {} output",
                resolved.encoder_name(), hdr_output ? "PQ/HLG 10-bit" : "SDR");
            return false;
        }
        codec_->pix_fmt = target_format;
    }

    // One color-boundary authority (encoder_bitstream_contract): the encoder
    // applies exactly the values the splice gate compares against.
    {
        const auto color = encoder_color_contract(options_.color_transform.output);
        codec_->color_range = color.color_range;
        codec_->colorspace = color.color_space;
        codec_->color_trc = color.color_trc;
        codec_->color_primaries = color.color_primaries;
    }
    const bool hdr_output = options_.color_transform.output == color::ColorSpace::PQ ||
                            options_.color_transform.output == color::ColorSpace::HLG;
    output_frame_format_ = runtime::FrameFormat{
        hdr_output ? runtime::PixelFormat::Yuv420P10
            : options_.direct_yuv_mode && options_.input_format == PipePixelFormat::P010
                ? runtime::PixelFormat::P010 : runtime::PixelFormat::Nv12,
        hdr_output
            ? runtime::ColorPrimaries::Bt2020 : runtime::ColorPrimaries::Bt709,
        detail::transfer_from_output_space(options_.color_transform.output),
        hdr_output ? runtime::ColorMatrix::Bt2020Ncl : runtime::ColorMatrix::Bt709,
        runtime::ColorRange::Limited,
        runtime::ChromaLocation::Left,
        runtime::AlphaMode::Opaque};
    return true;
}

bool NativeAvEncoder::apply_resolved_encoder_options() {
    const auto& resolved = *resolved_encoder_config_;

    // Everything below is driven by `resolved` (from
    // resolve_encoder_config). NativeAvEncoder no longer interprets
    // rate-control/preset/tune strings: the resolver already validated the
    // combination for this backend and produced the exact option set.
    {
        const std::string& preset = resolved.preset();
        if (!preset.empty() &&
            !detail::set_codec_option_checked(codec_, "preset", preset)) return false;
    }
    if (const auto crf = resolved.crf()) {
        char crf_str[16];
        snprintf(crf_str, sizeof(crf_str), "%d", *crf);
        if (!detail::set_codec_option_checked(codec_, "crf", crf_str)) return false;
    }
    if (resolved.bitrate()) codec_->bit_rate = *resolved.bitrate();
    const std::string& codec_name = resolved.encoder_name();
    if (codec_name == "libx264" || codec_name == "libx264rgb") {
        if (options_.encode_threads > 0) {
            char threads_str[16];
            snprintf(threads_str, sizeof(threads_str), "%d", options_.encode_threads);
            if (!detail::set_codec_option_checked(codec_, "threads", threads_str)) return false;
        } else if (!detail::set_codec_option_checked(codec_, "threads", "auto")) {
            return false;
        }
        if (!detail::set_codec_option_checked(codec_, "thread_type", "frame")) return false;
    }
    if (const auto tune = resolved.tune()) {
        // The resolver only ever emits a tune on software (libx264-family)
        // backends; NVENC + tune fails during resolution.
        if (!detail::set_codec_option_checked(codec_, "tune", *tune)) return false;
    }
    // Fix 5 supported-config path: constant-QP on the software encoder.
    // The FFmpeg libx264 wrapper switches to CQP when `qp` is set; the
    // resolver guarantees a validated qp in [0, 69] whenever this branch is
    // reached (software + ConstantQp). NVENC CQP is handled in the
    // gpu_nvenc_ branch below.
    if (!gpu_nvenc_ &&
        resolved.rate_control() == ResolvedEncoderRateControl::ConstantQp &&
        (codec_name == "libx264" || codec_name == "libx264rgb")) {
        const int qp = resolved.qp().value_or(-1);
        if (qp < 0 || qp > 69) {
            spdlog::error(
                "[native_av] Internal error: software constant-QP without a "
                "validated qp reached open()");
            return false;
        }
        char qp_str[16];
        snprintf(qp_str, sizeof(qp_str), "%d", qp);
        if (!detail::set_codec_option_checked(codec_, "qp", qp_str)) return false;
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
        if (!detail::set_codec_option_checked(codec_, "surfaces", surf_str)) {
            return false;
        }
        // Forced keyframes are requested through the frame's pict_type (the
        // segment boundary, the duplicate-run anchor and the cross-item
        // session restart all rely on that request becoming a real IDR). This
        // NVENC wrapper drops a forced pict_type entirely unless the private
        // `forced-idr` option is set: the boundary frame then encodes as a
        // non-key P packet, and the segmented mux writer rejects it at the
        // exact segment start (fail-closed `AVERROR_EXTERNAL` after a few
        // packets). With the option set, a forced frame comes out as a true
        // IDR key packet — the "forced keyframe at every segment start, with
        // a GOP cut" contract. The option is inert for unforced frames.
        if (!detail::set_codec_option_checked(codec_, "forced-idr", "1")) {
            return false;
        }
        switch (resolved.rate_control()) {
            case ResolvedEncoderRateControl::ConstantQp: {
                if (!detail::set_codec_option_checked(codec_, "rc", "constqp")) {
                    return false;
                }
                char qp_str[16];
                snprintf(qp_str, sizeof(qp_str), "%d", resolved.qp().value_or(-1));
                if (!detail::set_codec_option_checked(codec_, "qp", qp_str)) {
                    return false;
                }
                // Cross-item session reuse (Fix 5): the wrapper's output FIFO
                // retains un-received packets until flush or more input, so a
                // buffered tail defeats the strict frames==packets boundary
                // certification at close() and the session can never park.
                // delay=0 removes the wrapper-side output buffering (no
                // reordering, bitstream shape unchanged): every submitted
                // frame's packet is retrievable at the closed boundary, making
                // the park/adopt certification determinate for the constqp
                // family. Non-constqp NVENC keeps the historical buffering.
                if (!detail::set_codec_option_checked(codec_, "delay", "0")) {
                    spdlog::warn(
                        "[native_av] NVENC constqp: delay=0 unsupported in this "
                        "FFmpeg build; cross-item session reuse stays "
                        "uncertifiable (flush-at-close fallback applies)");
                }
                break;
            }
            case ResolvedEncoderRateControl::Vbr:
                if (!detail::set_codec_option_checked(codec_, "rc", "vbr")) {
                    return false;
                }
                break;
            case ResolvedEncoderRateControl::Cbr:
                if (!detail::set_codec_option_checked(codec_, "rc", "cbr")) {
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
    return true;
}

std::string NativeAvEncoder::compute_codec_session_fingerprint() const {
    // Fail-closed first: lanes whose configuration carries axes this cache
    // does not model (spliced/borrowed stream declarations, static HDR
    // mastering metadata, OCIO output views) never share a session.
    if (!resolved_encoder_config_) return {};
    if (options_.borrowed_declared_stream || options_.hdr_mastering_metadata ||
        !options_.ocio_display.empty()) {
        return {};
    }
    const auto& resolved = *resolved_encoder_config_;
    // Every field that flows into configure_codec_and_surface() or
    // apply_resolved_encoder_options() must participate: a mismatch here
    // would hand a codec configured for one stream to a job that needs
    // another.
    return fmt::format(
        "c3d.codec-session.v1|enc={}|preset={}|crf={}|bitrate={}|tune={}|"
        "async={}|rc={}|qp={}|{}x{}|fps={}/{}|threads={}|gpu={}|"
        "out={}|gamma={}|dyuv={}|in={}",
        resolved.encoder_name(), resolved.preset(),
        resolved.crf().value_or(-1),
        static_cast<long long>(resolved.bitrate().value_or(0)),
        resolved.tune().value_or(std::string{}),
        resolved.async_depth(),
        static_cast<int>(resolved.rate_control()),
        resolved.qp().value_or(-1),
        options_.width, options_.height,
        options_.fps_num, options_.fps_den,
        options_.encode_threads,
        gpu_nvenc_ ? 1 : 0,
        static_cast<int>(options_.color_transform.output),
        options_.color_transform.apply_gamma ? 1 : 0,
        options_.direct_yuv_mode ? 1 : 0,
        static_cast<int>(options_.input_format));
}

bool NativeAvEncoder::codec_session_config_supported() const {
    if (!resolved_encoder_config_) return false;
    const auto& resolved = *resolved_encoder_config_;
    if (resolved.rate_control() != ResolvedEncoderRateControl::ConstantQp) {
        return false;
    }
    if (!resolved.qp().has_value()) return false;
    const std::string& encoder_name = resolved.encoder_name();
    if (gpu_nvenc_) {
        // NVENC constant-QP family (h264_nvenc/hevc_nvenc): constqp carries no
        // adaptive rate-control feedback on the driver side (the CRF-history
        // leak x264 showed does not exist there), the wrapper sets no
        // lookahead and no B-frame tail in this build's defaults, and the
        // container boundary is self-certified at close(): the strict
        // frames==packets equality is evaluated before park, so any in-flight
        // frame (surfaces queue, lookahead) falls back to the historical
        // flush path and parks nothing — fail-closed by construction, not by
        // assumption. `forced-idr` is set for every NVENC open, so the
        // cross-item restart's pict_type request becomes a real IDR and the
        // adopted session reproduces exactly the per-item forced-keyframe/PTS
        // restart contract the segmented P4 writer certifies
        // (TestSegmentedSessionMatchesIndependentSingleItemRenders).
        // The fingerprint pins every other axis (preset p1, async depth,
        // resolution, fps, color, direct-yuv, input format).
        return encoder_name == "h264_nvenc" || encoder_name == "hevc_nvenc";
    }
    if (encoder_name != "libx264" && encoder_name != "libx264rgb") {
        return false;
    }
    // Canonical tune spelling as emitted by the resolver (see the allowed
    // x264 tune vocabulary); only zero-latency reaches the packet boundary.
    const auto tune = resolved.tune();
    if (!tune || *tune != "zerolatency") return false;
    // Frame threading keeps work in flight and defeats both the packet
    // boundary and determinism; 0 means engine-default (auto), also excluded.
    if (options_.encode_threads != 1) return false;
    return true;
}

bool NativeAvEncoder::try_adopt_codec_session() {
    if (codec_session_fingerprint_.empty() ||
        !detail::codec_session_reuse_enabled()) {
        return true;
    }
    // Defense in depth: the park site already gated eligibility, and the
    // fingerprint pins every session-shaping axis, but an adopter whose own
    // configuration is not supported must never reuse — fail closed.
    if (!codec_session_config_supported()) return true;
    auto session = detail::acquire_codec_session(codec_session_fingerprint_);
    if (!session || !session->codec) return true;
    // Fix 5 — only an UNFLUSHED parked session is adoptable: an EOS-flushed
    // codec rejects new frames forever (FFmpeg, verified empirically
    // 2026-10-26), so adopting one would fail on the first write_frame. The
    // park site self-certifies `unflushed`; anything else is discarded here
    // fail-closed (the session destructor frees the codec).
    if (!session->unflushed) {
        spdlog::warn(
            "[native_av] discarded parked codec session for fingerprint '{}': "
            "only unflushed sessions can be adopted",
            codec_session_fingerprint_);
        return true;
    }
    // Replace the context phase 3 just allocated with the cached, already
    // open one. The per-instance surface state configured in phase 3 (CUDA
    // refs, stream, compositor warm-up) stays as configured; the cached codec
    // carries its own hw_frames_ctx reference.
    avcodec_free_context(&codec_);
    codec_ = session->codec;
    session->codec = nullptr;
    bitstream_contract_ = session->contract;
    bitstream_contract_probed_ = true;
    // Reset the codec for the new frame/PTS timeline. The first frame of the
    // new container is forced intra (session_restart_intra_pending_) so this
    // item's MP4 starts on a keyframe and the adopted codec's previous GOP
    // cannot leak into the new stream.
    avcodec_flush_buffers(codec_);
    codec_session_adopted_ = true;
    session_restart_intra_pending_ = true;
    spdlog::info(
        "[native_av] codec session adopted for cross-job reuse (fingerprint '{}')",
        codec_session_fingerprint_);
    return true;
}

} // namespace chronon3d::cli

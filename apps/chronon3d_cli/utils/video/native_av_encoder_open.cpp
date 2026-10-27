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









NativeAvEncoder::NativeAvEncoder(
    std::shared_ptr<media::VideoDeviceRuntime> device_runtime)
    : device_runtime_(std::move(device_runtime)) {}

const BitstreamTargetContract& NativeAvEncoder::encoder_target_contract() {
    std::lock_guard<std::mutex> lock(mutex_);
    return encoder_target_contract_unlocked();
}

const BitstreamTargetContract&
NativeAvEncoder::encoder_target_contract_unlocked() {
    // Refresh once, after the first encoded frame: encoders publish profile /
    // level / SPS-PPS with their first packet, so a contract read before that
    // is legitimately incomplete (and therefore fail-closed). The one-shot
    // guard keeps the accessor free of per-frame decoder probes; an encoder
    // that never publishes them simply stays unmaterialized.
    if (!bitstream_contract_.encoder_materialized && !bitstream_contract_probed_ &&
        frames_written_ >= 1 && codec_ && resolved_encoder_config_) {
        bitstream_contract_probed_ = true;
        bitstream_contract_ = encoder_bitstream_contract_from_codec(
            *codec_, options_, *resolved_encoder_config_);
        if (bitstream_contract_.encoder_materialized) {
            spdlog::info(
                "[native_av] encoder bitstream contract materialized: "
                "profile={} level={} pix_fmt={} parameter_sets={}",
                bitstream_contract_.profile, bitstream_contract_.level,
                static_cast<int>(bitstream_contract_.pixel_format),
                bitstream_contract_.parameter_sets.size());
        }
    }
    return bitstream_contract_;
}

// ── open() orchestrator (TICKET-HOTSPOT-DECOMPOSITION A6) ────────────────
// The phases below own the work; this function only sequences them and keeps
// the public contract (bool + the same spdlog reason) unchanged.
bool NativeAvEncoder::open(const FfmpegPipeOptions& options) {
    if (mux_ || codec_) {
        spdlog::error("[native_av] Encoder already open");
        return false;
    }

    open_complete_ = false;

    if (!detail::validate_encoder_options(options)) {
        const auto output_space = options.color_transform.output;
        if (output_space == color::ColorSpace::LinearBT2020) {
            spdlog::error(
                "[native_av] LinearBT2020 output is not available: video output requires an explicit transfer encoding");
        } else if (output_space == color::ColorSpace::ACEScg) {
            spdlog::error(
                "[native_av] ACEScg output requires an OCIO transform, unavailable in the CPU video writer");
        } else {
            spdlog::error("[native_av] Invalid encoder options (w={}, h={}, fps={}/{}, path='{}')",
                          options.width, options.height,
                          options.canonical_fps_num(), options.canonical_fps_den(),
                          options.output_path);
        }
        return false;
    }

    // Phase 1 — clear the per-open state and publish the caller's options.
    reset_open_state(options);
#ifdef CHRONON3D_ENABLE_OCIO
    ocio_output_transform_.reset();
    if (!options_.ocio_display.empty()) {
        std::string error;
        auto transform = color::OcioColorTransform::from_display_view(
            options_.ocio_config_path, options_.ocio_source_space,
            options_.ocio_display, options_.ocio_view, &error);
        if (!transform) {
            spdlog::error("[native_av] OCIO output view setup failed: {}", error);
            return false;
        }
        ocio_output_transform_ = std::make_unique<color::OcioColorTransform>(
            std::move(*transform));
        options_.color_transform.apply_gamma = false;
        options_.direct_yuv_mode = false;
    }
#else
    if (!options_.ocio_display.empty()) {
        spdlog::error("[native_av] OCIO output view requested but this build has OCIO disabled");
        return false;
    }
#endif

    // Phase 2 — resolve + validate the encoder configuration BEFORE any
    // FFmpeg/decoder/CUDA/render setup.
    if (!resolve_open_configuration()) return false;
    codec_session_fingerprint_ = compute_codec_session_fingerprint();

    // Phase 3 — allocate the codec context and select the surface.
    if (!configure_codec_and_surface()) return false;

    // Phase 3b — cross-item codec-session adoption (opt-in, fail-closed).
    if (!try_adopt_codec_session()) return false;

    // Phase 4 — apply the RESOLVED configuration verbatim. Skipped on session
    // adoption: the cached codec is already open and carries these options.
    if (!codec_session_adopted_ && !apply_resolved_encoder_options()) return false;

    // Phase 5 — open the codec and probe the encoder-owned bitstream contract.
    // Skipped on session adoption: the cached codec is already open and its
    // live bitstream contract was restored in phase 3b.
    if (!codec_session_adopted_ && !open_codec_and_probe_contract()) return false;

    const std::string filename = options_.output_path;

    // Phase 6 — container ownership / mux wiring.
    if (!wire_container(filename)) return false;

    // Phase 7 — frame buffers and final validation.
    if (!finalize_open(filename)) return false;
    // Cross-item session eligibility (fail-closed): only a healthy owned
    // container under a complete fingerprint AND the Fix 5 supported
    // configuration may be parked for the next job. Adaptive rate control
    // (CRF), B-frames/lookahead and frame threading all carry encoder state
    // across jobs and change the next job's pixels; parking them would trade
    // correctness for teardown savings.
    codec_session_eligible_ = !codec_session_fingerprint_.empty() &&
        !borrows_mux_ && options_.output_segments.empty() &&
        codec_session_config_supported();
    return true;
}

void NativeAvEncoder::reset_open_state(const FfmpegPipeOptions& options) {
    segmented_mux_.reset();
    owned_mux_.reset();
    mux_ = nullptr;
    borrows_mux_ = false;
    options_ = options;
    options_.fps_num = options.canonical_fps_num();
    options_.fps_den = options.canonical_fps_den();
    frames_written_ = 0;
    encoder_packets_received_ = 0;
    codec_packets_received_ = 0;
    codec_frames_submitted_ = 0;
    mux_packets_written_ = 0;
    first_encoder_packet_pts_ = -1;
    last_encoder_packet_pts_ = -1;
    // P1 run-anchor duplication state (pooled re-open must not inherit a
    // previous job's anchor or discard windows).
    duplicate_seed_source_.reset();
    duplicate_anchor_.reset();
    last_video_key_packet_.reset();
    last_video_key_packet_pts_ = -1;
    duplicate_run_start_pts_ = -1;
    duplicate_prerun_packets_received_ = 0;
    duplicate_prerun_packets_target_ = 0;
    duplicate_anchor_pts_ = -1;
    duplicate_replicated_through_pts_ = -1;
    duplicate_discard_ranges_.clear();
    duplicate_run_active_ = false;
    duplicate_run_mode_ = DuplicateRunMode::Undecided;
    last_real_frame_pts_ = -1;
    last_real_packet_seen_ = false;
    last_real_packet_was_key_ = false;
    last_real_key_packet_.reset();
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    frames_submitted_ = 0;
    cuda_pending_peak_ = 0;
    cuda_backpressure_wait_count_ = 0;
    pending_cuda_frames_.clear();
    reusable_cuda_frames_.clear();
#endif

    last_conversion_key_ = video::ConversionCacheKey{};
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
    bitstream_contract_ = BitstreamTargetContract{};
    bitstream_contract_probed_ = false;
    borrows_mux_ = false;
    mux_ = nullptr;
    owned_mux_.reset();
    borrowed_stream_reproduced_ = false;
    borrowed_stream_checked_ = false;
    // Cross-item codec-session state (a pooled re-open must not inherit a
    // previous job's session decisions).
    codec_session_fingerprint_.clear();
    codec_session_adopted_ = false;
    codec_session_eligible_ = false;
    session_restart_intra_pending_ = false;
}



















} // namespace chronon3d::cli

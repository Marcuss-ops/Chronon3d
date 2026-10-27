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

bool NativeAvEncoder::open_codec_and_probe_contract() {
    const auto& resolved = *resolved_encoder_config_;

    // Phase 3 resolved the encoder by name; re-resolving here keeps this phase
    // self-contained and yields exactly the same AVCodec instance the context
    // was allocated with (codec_->codec is the allocation-time fallback).
    const AVCodec* encoder =
        avcodec_find_encoder_by_name(resolved.encoder_name().c_str());
    if (!encoder) encoder = codec_->codec;

    const auto nvenc_t0 = Clock::now();
    if (options_.hdr_mastering_metadata &&
        !detail::configure_hdr_static_metadata(codec_, *options_.hdr_mastering_metadata)) {
        spdlog::error("[native_av] failed to configure static HDR metadata");
        return false;
    }
    if (avcodec_open2(codec_, encoder, nullptr) < 0) {
        spdlog::error("[native_av] avcodec_open2 failed");
        return false;
    }
    open_nvenc_ms_ = elapsed_ms(nvenc_t0);

    // P0-5A — probe the ENCODER-OWNED bitstream contract. The compressed
    // source is never consulted here. Encoders publish their output facts
    // (profile / level / SPS-PPS) with the first packet, so this first probe is
    // usually still unmaterialized; encoder_target_contract() refreshes it on
    // demand and the splice gate stays fail-closed until it is complete.
    bitstream_contract_
        = encoder_bitstream_contract_from_codec(*codec_, options_, resolved);
    spdlog::info(
        "[native_av] encoder bitstream contract: codec={} profile={} level={} "
        "pix_fmt={} parameter_sets={} materialized={}",
        static_cast<int>(bitstream_contract_.codec), bitstream_contract_.profile,
        bitstream_contract_.level,
        static_cast<int>(bitstream_contract_.pixel_format),
        bitstream_contract_.parameter_sets.size(),
        bitstream_contract_.encoder_materialized);
    return true;
}

} // namespace chronon3d::cli

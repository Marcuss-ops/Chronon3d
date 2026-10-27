#pragma once

#include "encoder_bitstream_contract.hpp"
#include "native_av_encoder.hpp"

#include <chrono>
#include <memory>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace chronon3d::cli::detail {

[[nodiscard]] runtime::TransferFunction transfer_from_output_space(
    color::ColorSpace output) noexcept;
[[nodiscard]] bool set_codec_option_checked(
    AVCodecContext* codec, const char* key, const std::string& value);
[[nodiscard]] bool configure_hdr_static_metadata(
    AVCodecContext* codec, const color::HdrMasteringMetadata& metadata);
[[nodiscard]] bool validate_encoder_options(const FfmpegPipeOptions& options);

using NativeAvClock = std::chrono::steady_clock;
[[nodiscard]] double elapsed_ms(const NativeAvClock::time_point& start) noexcept;

// ── Cross-item codec session (TICKET-RESIDENT-RENDER-ENGINE (P2) item 2 /
// TICKET-BATCH-SUPERJOB (P4) item 1) ──────────────────────────────────────
//
// The measured per-item fixed cost is dominated by the encoder lifecycle
// (`encoder_open_nvenc` + `encode_close` ≈ 259 ms per artifact): one NVENC
// session is created and destroyed for every item even when consecutive items
// resolve to the SAME encoder configuration. A codec session cached across
// items turns that into one session per batch while every item keeps its own
// MP4 container (the MP4-per-item contract with the final worker is
// preserved: one muxer, one PTS timeline, one forced-IDR start per item).
//
// Cross-job adoption is opt-in (CHRONON_ENCODER_SESSION_REUSE=1) and
// additionally gated on the Fix 5 supported configuration (software x264,
// constant-QP, zerolatency, one thread): EOS-flushed codecs reject new
// frames, and packet-complete unflushed codecs with adaptive rate control
// (CRF) retain history and change the next job's pixels. A certified packet
// boundary alone does not prove independent-render parity — CQP has no
// adaptive state, which is what makes the supported shape shareable.
// P4's single-session segmented writer does not use this cross-job cache.

/// One cached, still-open codec session with the contract of its live
/// bitstream. `codec` is transferred in and out of the struct: whichever side
/// holds a non-null `codec` owns it and frees it on destruction.
struct CodecSession {
    AVCodecContext* codec{nullptr};
    BitstreamTargetContract contract{};
    std::string fingerprint{};
    // Self-certified by the park site: true only when close() parked this
    // session WITHOUT an EOS flush (certified packet boundary). Only such
    // sessions may be cached or adopted — an EOS-flushed codec rejects new
    // frames forever, so adopting one would fail on the first write.
    bool unflushed{false};

    CodecSession() = default;
    ~CodecSession();
    CodecSession(const CodecSession&) = delete;
    CodecSession& operator=(const CodecSession&) = delete;
};

/// Cross-job codec-session capability seam: true only with the documented
/// opt-in flag set. Park/adopt are additionally gated per configuration
/// (NativeAvEncoder::codec_session_config_supported), so the flag alone
/// never enables reuse of an uncertified session shape.
[[nodiscard]] bool codec_session_reuse_enabled() noexcept;

/// Returns the cached session when the cache holds one AND its fingerprint
/// matches exactly; the cache entry is emptied on success (single-entry
/// cache). Null when there is nothing to adopt.
[[nodiscard]] std::shared_ptr<CodecSession> acquire_codec_session(
    const std::string& fingerprint) noexcept;

/// Parks a healthy finished session for the next same-configuration open.
/// The single cache slot is replaced atomically (the displaced session is
/// destroyed). A null session, disabled reuse, an empty fingerprint or a
/// session that is not `unflushed` frees instead of caching — fail-closed.
void release_codec_session(std::shared_ptr<CodecSession> session) noexcept;

} // namespace chronon3d::cli::detail

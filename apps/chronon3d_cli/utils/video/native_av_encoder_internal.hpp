#pragma once

#include "encoder_bitstream_contract.hpp"

#include <chrono>
#include <memory>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace chronon3d::cli::detail {

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
// Cross-job adoption is disabled even with CHRONON_ENCODER_SESSION_REUSE=1:
// EOS-flushed codecs reject new frames, and packet-complete unflushed codecs
// can retain adaptive rate-control history and change the next job's pixels.
// A certified packet boundary alone does not prove independent-render parity.
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

/// Cross-job codec-session capability seam; currently always false until
/// independent-render pixel parity is certified for a supported configuration.
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

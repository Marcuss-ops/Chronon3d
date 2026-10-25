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
// This cache is intentionally disabled for now. A finalized MP4 requires
// sending encoder EOF, after which FFmpeg rejects new frames even if
// avcodec_flush_buffers is called. Reuse can only be restored when a batch
// writer owns the complete unflushed codec lifetime and splits independent
// containers at certified packet boundaries.

/// One cached, still-open codec session with the contract of its live
/// bitstream. `codec` is transferred in and out of the struct: whichever side
/// holds a non-null `codec` owns it and frees it on destruction.
struct CodecSession {
    AVCodecContext* codec{nullptr};
    BitstreamTargetContract contract{};
    std::string fingerprint{};

    CodecSession() = default;
    ~CodecSession();
    CodecSession(const CodecSession&) = delete;
    CodecSession& operator=(const CodecSession&) = delete;
};

/// Reuse of an encoder after a finalized MP4 is not supported by FFmpeg.
/// Kept as a capability seam; returns false until a batch muxer owns the
/// unflushed session lifecycle.
[[nodiscard]] bool codec_session_reuse_enabled() noexcept;

/// Returns the cached session when the cache holds one AND its fingerprint
/// matches exactly; the cache entry is emptied on success (single-entry
/// cache). Null when there is nothing to adopt.
[[nodiscard]] std::shared_ptr<CodecSession> acquire_codec_session(
    const std::string& fingerprint) noexcept;

/// Parks a healthy finished session for the next same-configuration open.
/// The single cache slot is replaced atomically (the displaced session is
/// destroyed). A null session, disabled reuse or an empty fingerprint frees
/// instead of caching.
void release_codec_session(std::shared_ptr<CodecSession> session) noexcept;

} // namespace chronon3d::cli::detail

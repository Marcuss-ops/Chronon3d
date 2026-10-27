#include "native_av_encoder.hpp"
#include "native_av_encoder_internal.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <mutex>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#endif

namespace chronon3d::cli {
using Clock = detail::NativeAvClock;
using detail::elapsed_ms;

namespace detail {
namespace {
// Single-entry cross-item codec-session cache (see the rationale in
// native_av_encoder_internal.hpp). One entry is enough: the daemon executes
// one render job at a time per lane (CHRONON_RENDER_JOB_SLOTS=1), so
// consecutive items of a batch reuse exactly one session.
std::mutex g_codec_session_cache_mutex;
std::shared_ptr<CodecSession> g_codec_session_cache;
}  // namespace

CodecSession::~CodecSession() {
    if (codec) avcodec_free_context(&codec);
}

bool codec_session_reuse_enabled() noexcept {
    // Cross-job codec adoption is opt-in via CHRONON_ENCODER_SESSION_REUSE=1
    // AND additionally gated per configuration (Fix 5 supported-config gate
    // in NativeAvEncoder::codec_session_config_supported): only a constant-QP
    // zero-latency single-thread software session may be parked or adopted.
    //
    // Safety invariant (why the default/CRF path stays fail-closed): a
    // completed MP4 requires sending end-of-stream to its encoder, after
    // which FFmpeg rejects new frames with AVERROR_EOF and
    // avcodec_flush_buffers does not make the encoder reusable. Verified
    // empirically 2026-10-26 (test "Cross-item codec session reuse stays
    // fail-closed").
    //
    // NativeAvEncoder::close() therefore parks the codec UNFLUSHED and only
    // at a certified packet boundary (every frame submitted to the codec
    // already produced its packet, so the container is complete without EOS).
    // An EOS-flushed encoder must never be parked; the park site is gated on
    // that boundary, not on this flag.
    // Packet completeness is insufficient: the moving-content regression
    // proves that an unflushed x264 CRF session retains adaptive state and
    // changes decoded pixels in the following job, even after codec flush —
    // hence the supported-config gate (CQP has no adaptive state). P4's
    // one-session segmented writer is unaffected: it never adopts a codec
    // across independent jobs.
    if (const char* flag = std::getenv("CHRONON_ENCODER_SESSION_REUSE")) {
        return flag[0] == '1' && flag[1] == '\0';
    }
    return false;
}

std::shared_ptr<CodecSession> acquire_codec_session(
    const std::string& fingerprint) noexcept {
    if (!codec_session_reuse_enabled() || fingerprint.empty()) return nullptr;
    std::lock_guard<std::mutex> lock(g_codec_session_cache_mutex);
    if (!g_codec_session_cache ||
        g_codec_session_cache->fingerprint != fingerprint) {
        return nullptr;
    }
    auto session = std::move(g_codec_session_cache);
    g_codec_session_cache.reset();
    return session;
}

void release_codec_session(std::shared_ptr<CodecSession> session) noexcept {
    // Fail-closed: only an UNFLUSHED session may enter the cache. An
    // EOS-flushed codec rejects new frames forever, so caching one would hand
    // the next adopter a dead codec (Fix 5).
    if (!session || !codec_session_reuse_enabled() ||
        session->fingerprint.empty() || !session->codec || !session->unflushed) {
        return;  // the deleter frees the codec
    }
    std::lock_guard<std::mutex> lock(g_codec_session_cache_mutex);
    g_codec_session_cache = std::move(session);
}
}  // namespace detail

void NativeAvEncoder::park_codec_session_for_reuse() {
    if (!codec_ || !detail::codec_session_reuse_enabled()) return;
    auto session = std::make_shared<detail::CodecSession>();
    session->codec = codec_;
    codec_ = nullptr;
    // Self-certification: this site is only reachable from the reuse_unflushed
    // branch of close() (certified packet boundary, no EOS sent).
    session->unflushed = true;
    // The contract of the LIVE session follows the session, not the instance:
    // refresh it once here so the next adopter starts from the materialized
    // parameter sets this run actually produced.
    bitstream_contract_probed_ = true;
    session->contract = encoder_target_contract_unlocked();
    session->fingerprint = codec_session_fingerprint_;
    // codec_session_adopted_ intentionally survives close(): it records how
    // this item obtained its session and is only reset by the next open.
    detail::release_codec_session(std::move(session));
    spdlog::info(
        "[native_av] codec session parked for cross-item reuse (fingerprint '{}')",
        codec_session_fingerprint_);
}

bool NativeAvEncoder::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!codec_) {
        if (!open_complete_) abort_open();
        return true;
    }
    open_complete_ = false;

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    while (gpu_nvenc_ && !pending_cuda_frames_.empty()) {
        if (!drain_ready_cuda_frames(true)) {
            spdlog::error("[native_av] failed to drain pending CUDA frames");
            break;
        }
    }
#endif

    const auto t_flush0 = Clock::now();
    bool flush_ok = false;
    int flush_result = AVERROR(EAGAIN);
    // Fix 5 — cross-job codec session (P2): at a CERTIFIED PACKET BOUNDARY
    // (every frame submitted to the codec already produced its packet) the
    // container is complete without end-of-stream. First receive whatever the
    // encoder has already finished but the write path left uncollected (the
    // tail submits can lag one drain); this is receive-only, no EOS.
    const bool preflush_drained = drain_packets();
    // Skip the EOS flush at the boundary and keep the codec UNFLUSHED for the
    // park/adopt cache: an EOS-flushed codec rejects new frames forever
    // (FFmpeg, verified empirically 2026-10-26), so only an unflushed,
    // boundary-complete session may be parked. Every other close keeps the
    // historical flush path and never parks. A frame-threaded encoder keeps
    // work in flight, so the strict frames==packets equality is what
    // certifies the boundary; anything else falls back to flush (fail-closed).
    const bool reuse_unflushed =
        preflush_drained && codec_session_eligible_ &&
        detail::codec_session_reuse_enabled() && !duplicate_run_active_ &&
        codec_frames_submitted_ > 0 &&
        encoder_packets_received_ == codec_frames_submitted_;
    if (reuse_unflushed) {
        spdlog::info(
            "[native_av] close: packet-complete boundary ({} frames / {} packets); "
            "skipping EOS flush to keep the codec session reusable across jobs",
            codec_frames_submitted_, encoder_packets_received_);
        flush_ok = drain_packets();
        flush_result = 0;
    } else {
    spdlog::info("[native_av] close: sending encoder flush");
    // A frame threaded encoder can have more work in flight than the CLI
    // wrapper's old single send/drain sequence assumed. EAGAIN means output
    // must be received before the null frame can enter the codec; retry after
    // draining or delayed frames stay permanently unencoded in the MP4.
    for (int attempt = 0; attempt < 2; ++attempt) {
        flush_result = avcodec_send_frame(codec_, nullptr);
        if (flush_result == 0 || flush_result == AVERROR_EOF) {
            flush_ok = true;
            break;
        }
        if (flush_result != AVERROR(EAGAIN)) {
            spdlog::error("[native_av] encoder flush submit failed: {}", flush_result);
            break;
        }
        if (!drain_packets()) {
            spdlog::error("[native_av] packet drain failed while entering flush mode");
            break;
        }
    }
    if (!flush_ok && flush_result == AVERROR(EAGAIN)) {
        spdlog::error("[native_av] encoder flush remained EAGAIN after draining packets");
    }
    if (flush_ok && flush_result != AVERROR_EOF) {
        spdlog::info("[native_av] close: encoder flush accepted");
        flush_ok = drain_packets();
    }
    if (flush_ok && duplicate_run_active_ &&
        duplicate_run_mode_ == DuplicateRunMode::Replicate &&
        duplicate_replicated_through_pts_ <
            static_cast<std::int64_t>(frames_written_) - 1) {
        if (duplicate_prerun_packets_received_ <
                duplicate_prerun_packets_target_ ||
            !duplicate_anchor_) {
            spdlog::error(
                "[native_av] duplicate run could not drain its pre-run codec packets "
                "before close (received={} target={})",
                duplicate_prerun_packets_received_,
                duplicate_prerun_packets_target_);
            flush_ok = false;
        } else {
            flush_ok = emit_duplicate_replicas_through_(
                static_cast<std::int64_t>(frames_written_) - 1);
        }
    }
    }
    spdlog::info("[native_av] close: packets drained={}", flush_ok);
    double trailer_ms = 0.0;
    if (borrows_mux_) {
        // Caller-owned container: audio and the trailer belong to the caller
        // and happen exactly once, after every work unit of the schedule has
        // been submitted. Finalizing here would close the container after the
        // FIRST re-encoded GOP and drop every subsequent one.
        //
        // P0-5E — splice certification, at the last moment it is still
        // possible: this is the only point where the encoder has already
        // published its parameter sets AND the container is still open. The
        // container's stream was declared from the compressed source, so the
        // encoder must reproduce it or the container it just wrote into is
        // one a decoder cannot follow across the copy/reencode boundary.
        std::string reproduction_failure;
        if (options_.borrowed_declared_stream) {
            // Refresh first: an encoder that has produced packets by now has
            // published profile/level/SPS-PPS. A still-unmaterialized contract
            // stays fail-closed rather than being assumed equal.
            // close() already owns mutex_; use the lock-free internal accessor
            // to keep this operation non-recursive.
            const auto& materialized = encoder_target_contract_unlocked();
            borrowed_stream_checked_ = true;
            borrowed_stream_reproduced_ = encoder_reproduces_declared_stream(
                *options_.borrowed_declared_stream, materialized);
            if (!borrowed_stream_reproduced_) {
                reproduction_failure = fmt::format(
                    "declared codec={} profile={} level={} {}x{} sets={} vs "
                    "encoder codec={} profile={} level={} {}x{} sets={} "
                    "materialized={}",
                    static_cast<int>(options_.borrowed_declared_stream->codec),
                    options_.borrowed_declared_stream->profile,
                    options_.borrowed_declared_stream->level,
                    options_.borrowed_declared_stream->width,
                    options_.borrowed_declared_stream->height,
                    options_.borrowed_declared_stream->parameter_sets.size(),
                    static_cast<int>(materialized.codec), materialized.profile,
                    materialized.level, materialized.width, materialized.height,
                    materialized.parameter_sets.size(),
                    materialized.encoder_materialized);
            }
        }
        spdlog::info(
            "[native_av] close: borrowed mux session left open for the caller "
            "(no audio, no trailer); stream reproduction verified={}",
            borrowed_stream_checked_ ? (borrowed_stream_reproduced_ ? "true" : "false")
                                     : "not-required");
        // The container is the caller's from here on: never touch it again.
        mux_ = nullptr;
        borrows_mux_ = false;
        if (!reproduction_failure.empty()) {
            spdlog::error(
                "[native_av] FAIL_CLOSED: the encoder did not reproduce the "
                "borrowed container's declared stream ({}); this work unit "
                "cannot be spliced with the copied GOPs in the same container",
                reproduction_failure);
            av_packet_free(&packet_);
            av_frame_free(&frame_);
            avcodec_free_context(&codec_);
            bitstream_contract_ = BitstreamTargetContract{};
            bitstream_contract_probed_ = false;
            return false;
        }
    } else {
        if (segmented_mux_) {
            const auto t_trailer0 = Clock::now();
            if (!segmented_mux_->finalize()) flush_ok = false;
            trailer_ms = elapsed_ms(t_trailer0);
        } else {
        if (audio_input_ && !mux_source_audio()) {
            spdlog::error("[native_av] failed to mux source audio");
        }
        const auto t_trailer0 = Clock::now();
        if (mux_) {
            spdlog::info("[native_av] close: finalizing mux session");
            if (!mux_->finalize()) flush_ok = false;
            spdlog::info("[native_av] close: mux session finalized");
        }
        trailer_ms = elapsed_ms(t_trailer0);
        }
    }
    const double flush_ms = elapsed_ms(t_flush0);
    native_flush_ms_ += flush_ms;
    native_trailer_ms_ += trailer_ms;

    if (profiling::g_current_counters) {
        profiling::g_current_counters->encoder_flush_wall_ms.fetch_add(
            static_cast<uint64_t>(flush_ms), std::memory_order_relaxed);
        profiling::g_current_counters->mux_finalize_wall_ms.fetch_add(
            static_cast<uint64_t>(trailer_ms), std::memory_order_relaxed);
    }

    spdlog::info(
        "[native_av] Closed native encoder — {} frames accepted, encoder_packets={} "
        "mux_packets={} packet_pts={}..{}, YUV cache: {} hits / {} misses",
        frames_written_, encoder_packets_received_, mux_packets_written_,
        first_encoder_packet_pts_, last_encoder_packet_pts_,
        cache_hits_, cache_misses_);
    // Cross-job codec session (Fix 5, opt-in): park the still-open codec for
    // the next same-configuration open instead of paying avcodec_open2 +
    // NVENC session setup per artifact (P2 item 2 / P4 item 1). Only an
    // UNFLUSHED, boundary-complete session is parked (reuse_unflushed): a
    // flushed codec is dead for FFmpeg and must be freed, not cached.
    if (flush_ok && reuse_unflushed) {
        park_codec_session_for_reuse();
    }
    // The encoder-owned bitstream contract describes a live session only; a
    // closed (or pooled) instance must not leak it into the next job.
    bitstream_contract_ = BitstreamTargetContract{};
    bitstream_contract_probed_ = false;
    // A container this encoder opened stays reachable through `mux_` after
    // close() so the hash/checksum telemetry remains readable until the
    // encoder is torn down (abort_open releases it). A borrowed container was
    // already detached above.
    return flush_ok;
}

NativeAvEncoder::~NativeAvEncoder() {
    shutdown_noexcept();
}

void NativeAvEncoder::shutdown_noexcept() noexcept {
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
        cuda::destroy(pending.ready);
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
    av_packet_free(&packet_);
    av_frame_free(&frame_);
    avcodec_free_context(&codec_);
    avformat_close_input(&audio_input_);
    audio_input_stream_ = -1;
    // Only the container this encoder opened is destroyed here; a borrowed
    // MuxSession belongs to the caller.
    owned_mux_.reset();
    segmented_mux_.reset();
    mux_ = nullptr;
    borrows_mux_ = false;
    packet_ = nullptr;
    frame_ = nullptr;
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
    av_buffer_unref(&cuda_frames_ref_);
    av_buffer_unref(&cuda_device_ref_);
    cuda_context_ = nullptr;
    gpu_nvenc_ = false;
#endif
    bitstream_contract_ = BitstreamTargetContract{};
    bitstream_contract_probed_ = false;
    open_complete_ = false;
}

} // namespace chronon3d::cli

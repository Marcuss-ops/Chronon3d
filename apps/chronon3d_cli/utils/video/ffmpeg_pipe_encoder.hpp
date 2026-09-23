#pragma once

#include <chronon3d/media/video/packet_assembler.hpp>
#include "bitstream_target_contract.hpp"
#include "encoder_config_resolution.hpp"

// =============================================================================
// ffmpeg_pipe_encoder.hpp — Abstract IVideoEncoder interface
// =============================================================================

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/runtime/frame_format.hpp>  // canonical pixel-format vocabulary
#include <chronon3d/runtime/render_surface_handle.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/color/output_transform.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <span>

namespace chronon3d::media::video {
struct DirectYuvFrame;
}

namespace chronon3d::cli {

/// P0-5E — a child render that must write into a container it does not own.
///
/// The hybrid GOP schedule produces one container for copied AND re-encoded
/// work units, so the re-encode lane borrows the caller's MuxSession instead
/// of opening a per-segment MP4. Everything the child needs to place and
/// certify itself lives here:
///
///   session          the open, caller-owned container (null = normal export)
///   declared_stream  the source facts the container was declared from; the
///                    child MUST reproduce them or the export fails closed
///
/// The child's placement in the container is not carried here: the owner sets
/// the work unit's origin on the MuxSession itself (set_video_origin), so the
/// schedule keeps exactly one interpretation and the container stays the one
/// timestamp authority.
struct BorrowedMuxTarget {
    chronon3d::media::MuxSession* session{nullptr};
    std::optional<BitstreamTargetContract> declared_stream{};
};

struct EncoderFrameTelemetry {
    double conversion_copy_ms{0.0};
    double pixel_format_convert_ms{0.0};
    double color_space_convert_ms{0.0};
    double encoder_ms{0.0};
    double pipe_write_ms{0.0};
    double backpressure_wait_ms{0.0};
    double pipe_write_cpu_ms{0.0};
    double pipe_backpressure_wait_ms{0.0};
    double native_convert_ms{0.0};
    double native_send_ms{0.0};
    double native_receive_ms{0.0};
    double native_mux_ms{0.0};
    uint64_t conversion_bytes_written{0};
    uint64_t encoder_staging_copy_bytes{0};
    uint64_t encoder_slots_allocated{0};
    uint64_t encoder_slot_reuses{0};
    double frame_submit_ms{0.0};
};

struct IVideoEncoder {
    virtual ~IVideoEncoder() = default;
    virtual bool open(const struct FfmpegPipeOptions& options) = 0;
    virtual bool write_frame_async(const Framebuffer& fb, std::shared_ptr<Framebuffer> owner) = 0;
    virtual bool write_direct_yuv(const media::video::DirectYuvFrame& frame) {
        (void)frame;
        return false;
    }
    virtual bool prepare_native_surface(
        graph::RenderBackend& backend,
        runtime::RenderSurfaceHandle destination) {
        (void)backend; (void)destination;
        return true;
    }
    virtual bool write_native_surface(
        graph::RenderBackend& backend,
        runtime::RenderSurfaceHandle source,
        runtime::RenderSurfaceHandle destination) {
        (void)backend; (void)source; (void)destination;
        return false;
    }
    virtual bool write_native_surface(
        graph::RenderBackend& backend,
        runtime::RenderSurfaceHandle source,
        runtime::RenderSurfaceHandle destination,
        std::span<const raster::BBox> dirty_regions) {
        (void)dirty_regions;
        return write_native_surface(backend, source, destination);
    }
    virtual bool write_prepared_native_surface(
        graph::RenderBackend& backend,
        runtime::RenderSurfaceHandle source,
        runtime::RenderSurfaceHandle destination) {
        return write_native_surface(backend, source, destination);
    }
    virtual bool write_prepared_native_surface(
        graph::RenderBackend& backend,
        runtime::RenderSurfaceHandle source,
        runtime::RenderSurfaceHandle destination,
        std::span<const raster::BBox> dirty_regions) {
        (void)dirty_regions;
        return write_prepared_native_surface(backend, source, destination);
    }
    virtual bool finish_native_surface(
        graph::RenderBackend& backend,
        runtime::RenderSurfaceHandle destination) {
        (void)backend; (void)destination;
        return true;
    }
    virtual bool poll_native_surface(
        graph::RenderBackend& backend,
        runtime::RenderSurfaceHandle destination) {
        (void)backend; (void)destination;
        return true;
    }
    virtual void set_counters(chronon3d::RenderCounters* counters) { (void)counters; }

    [[nodiscard]] virtual void* cuda_context() const { return nullptr; }
    virtual bool close() = 0;
    [[nodiscard]] virtual uint64_t frames_written() const = 0;
    [[nodiscard]] virtual EncoderFrameTelemetry last_frame_telemetry() const { return {}; }

    [[nodiscard]] virtual double native_convert_ms()     const { return 0.0; }
    [[nodiscard]] virtual double native_send_frame_ms()  const { return 0.0; }
    [[nodiscard]] virtual double native_backpressure_ms() const { return 0.0; }
    [[nodiscard]] virtual std::uint64_t native_cuda_pending_peak() const { return 0; }
    [[nodiscard]] virtual std::uint64_t native_cuda_backpressure_wait_count() const { return 0; }
    [[nodiscard]] virtual double native_flush_ms()       const { return 0.0; }
    [[nodiscard]] virtual double native_receive_packet_ms() const { return 0.0; }
    [[nodiscard]] virtual double native_mux_write_ms()   const { return 0.0; }
    [[nodiscard]] virtual double native_trailer_ms()     const { return 0.0; }
    [[nodiscard]] virtual double encoder_hwframe_get_buffer_ms() const { return 0.0; }
    [[nodiscard]] virtual double encoder_surface_acquire_ms() const { return 0.0; }
    [[nodiscard]] virtual double encoder_nvenc_submit_ms() const { return 0.0; }
    [[nodiscard]] virtual double encoder_queue_backpressure_wait_ms() const { return 0.0; }
    [[nodiscard]] virtual double encoder_packet_drain_ms() const { return 0.0; }
    [[nodiscard]] virtual double direct_yuv_cuda_launch_ms() const { return 0.0; }
    [[nodiscard]] virtual double direct_yuv_cuda_wait_ms() const { return 0.0; }
    [[nodiscard]] virtual double open_hw_ctx_ms() const { return 0.0; }
    [[nodiscard]] virtual double cuda_compositor_warmup_ms() const { return 0.0; }
    [[nodiscard]] virtual double open_nvenc_ms() const { return 0.0; }
    [[nodiscard]] virtual double open_mux_header_ms() const { return 0.0; }
    // Number of mux checksum finalizations that had to reread the finished
    // file because the container performed a seek/backpatch.
    [[nodiscard]] virtual std::uint64_t full_file_reread_count() const { return 0; }
    [[nodiscard]] virtual chronon3d::media::MuxHashMetrics hash_metrics() const {
        return {};
    }

    // Encoder settings actually applied at open() time (post-default
    // resolution). Empty/0 means "not applicable" (e.g. software encoder or
    // default virtual). Surfaced so certification can verify in the timing
    // sidecar which NVENC preset/RC/async_depth really ran.
    [[nodiscard]] virtual std::string applied_encoder_preset() const { return {}; }
    [[nodiscard]] virtual std::string applied_encoder_rate_control() const { return {}; }
    [[nodiscard]] virtual int applied_encoder_async_depth() const { return 0; }

    // Encoder settings the caller requested ("engine-default" when the value
    // was never explicitly requested). Together with the applied_* accessors
    // above this lets telemetry reveal requested vs resolved configuration
    // instead of pretending an engine default was an explicit user choice.
    [[nodiscard]] virtual std::string requested_encoder_rate_control() const { return {}; }
    [[nodiscard]] virtual std::string requested_encoder_preset() const { return {}; }

    [[nodiscard]] virtual double total_write_blocked_ms() const { return 0.0; }
    [[nodiscard]] virtual int    ffmpeg_pid() const { return -1; }
    /// Submit a source-compressed packet through the encoder's canonical mux.
    /// Kept at the end of the interface so existing encoder implementations
    /// retain their ABI prefix; pipe encoders fail closed because their child
    /// ffmpeg process owns the mux.
    virtual bool submit_copied_video(AVPacket& packet,
                                     AVRational source_time_base) {
        (void)packet;
        (void)source_time_base;
        return false;
    }
};

enum class PipePixelFormat {
    RGBA,
    YUV420P,
    NV12,
    P010
};

/// Wire name of a CLI pipe pixel format ("rgba", "yuv420p", "nv12", "p010").
///
/// Declared next to the enum so the ENUM and its NAME stay one concept, and
/// resolved through the canonical vocabulary in
/// <chronon3d/runtime/frame_format.hpp> so no second name table exists. The
/// canonical PARSER direction (`try_parse_pipe_pixfmt`) lives in
/// commands/video/common/video_export_common.hpp.
[[nodiscard]] inline std::string_view pipe_pixel_format_name(
    PipePixelFormat format) noexcept {
    switch (format) {
        case PipePixelFormat::RGBA:
            return runtime::pixel_format_name(runtime::PixelFormat::Rgba8Unorm);
        case PipePixelFormat::YUV420P:
            return runtime::pixel_format_name(runtime::PixelFormat::Yuv420P);
        case PipePixelFormat::NV12:
            return runtime::pixel_format_name(runtime::PixelFormat::Nv12);
        case PipePixelFormat::P010:
            return runtime::pixel_format_name(runtime::PixelFormat::P010);
    }
    return runtime::pixel_format_name(runtime::PixelFormat::Unknown);
}

struct FfmpegPipeOptions {
    int width{0};
    int height{0};
    // F3.1 — rational-only frame rate: fps_num/fps_den are the single
    // authority (the legacy scalar fps alias was removed from this struct and
    // from OutputOptions; the CLI normalises --fps into this rational).
    int fps_num{30};
    int fps_den{1};

    // ── Encoder configuration (rate control / quality) ────────────────────
    //
    // The `*_explicit` markers distinguish a USER REQUEST from a CHRONON
    // engine placeholder default. A default must never be indistinguishable
    // from an explicit request: the encoder-configuration resolver (see
    // encoder_config_resolution.hpp) rejects unsupported combinations only
    // when they were explicitly requested, and applies the deterministic
    // engine default otherwise. All plumbing (CLI render, render-plan, IPC,
    // video jobs) must set these markers from the original source of the
    // value; never leave an explicit value unmarked.
    std::string rate_control_mode{"crf"};
    bool rate_control_mode_explicit{false};
    int crf{18};
    bool crf_explicit{false};
    int qp{-1};
    bool qp_explicit{false};
    std::int64_t bitrate{0};
    bool bitrate_explicit{false};
    int async_depth{0};
    std::string preset{std::string(kDefaultEncoderPreset)};
    bool preset_explicit{false};
    std::string codec{"libx264"};
    std::string hardware_encoder{"none"};
    std::string output_path;
    PipePixelFormat input_format{PipePixelFormat::RGBA};
    std::string output_pix_fmt{"yuv420p"};
    bool verbose{false};
    color::OutputTransformOptions color_transform{};
    std::string tune;
    bool tune_explicit{false};
    std::string pipe_writer{"classic"};
    int encode_threads{0};
    bool direct_yuv_mode{false};

    // ── Caller-owned mux (splice lanes, P0-5C/5E) ─────────────────────────
    //
    // Null (the normal export) means the encoder owns its container: it opens
    // a MuxSession on `output_path` and finalizes it in close().
    //
    // Non-null means the CALLER owns the container and the encoder only
    // produces packets into it. Its video stream is already declared, so
    // close() must flush the codec and NOT finalize the container or write
    // audio — the caller does both exactly once, after every work unit
    // (copied AND re-encoded) of the schedule has been submitted. A borrowed
    // session must already be open.
    chronon3d::media::MuxSession* borrowed_mux{nullptr};

    // ── Borrowed-mux certification (P0-5E) ────────────────────────────────
    //
    // The stream facts the container was declared from (P0-5D declares it from
    // the compressed source). When present the encoder must REPRODUCE them
    // (profile / level / SPS-PPS included) or the export fails: the copied
    // GOPs in the same container do carry that stream, so an encoder that
    // emits a different one produces a container a decoder cannot follow
    // across the splice. Checked at close() with the one comparison authority,
    // after the encoder published its parameter sets.
    //
    // Placement is the container's business, not the encoder's: the owner of
    // the borrowed session calls MuxSession::set_video_origin() with the tick
    // the schedule reserved for the work unit being rendered, and the borrowed
    // producer submits with MuxVideoPlacement::ScheduledOrigin.
    std::optional<BitstreamTargetContract> borrowed_declared_stream{};
    std::string audio_source_path;
    /// Requested output audio sample rate (Hz); 0 keeps the source rate.
    int audio_target_sample_rate{0};
    double audio_start_seconds{0.0};
    double audio_end_seconds{0.0};

    // F3.1 — fps_num/fps_den are the single frame-rate authority. The
    // accessors are retained under their canonical_* names so stream/config
    // consumers read the one rational source; parity with
    // OutputOptions::frame_rate() is locked by the test "FfmpegFrameRate:
    // OutputOptions and FfmpegPipeOptions agree".
    [[nodiscard]] int canonical_fps_num() const noexcept { return fps_num; }
    [[nodiscard]] int canonical_fps_den() const noexcept { return fps_den; }
};

} // namespace chronon3d::cli

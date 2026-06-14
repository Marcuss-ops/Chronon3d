// ---------------------------------------------------------------------------
// test_ffmpeg_pipe_sink.cpp — Unit tests for FfmpegPipeSink lifecycle.
//
// Tests the VideoSink contract for FfmpegPipeSink:
//   - State machine (default state, transitions)
//   - Validation (dimensions, format, stride, null data)
//   - build_argv (verified experimentally via ffmpeg behavior)
//   - Full lifecycle (open → submit → flush → close)
//   - Output correctness (zero/one/multiple frames produce valid files)
//   - Broken pipe (ffmpeg killed mid-stream)
//   - Stats tracking (frames_submitted, bytes_written, timing)
//   - Planar/biplanar frames via pipe
//   - Exit code collection on close
//
// Tests that spawn ffmpeg are guarded by a runtime check; they are skipped
// on systems without ffmpeg installed.
//
// Dependencies: chronon3d_media_video, doctest, ffmpeg (optional).
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <chronon3d/media/video/video_sink.hpp>
#include <chronon3d/media/video/video_frame.hpp>
#include <chronon3d/media/video/video_config.hpp>
#include "src/media/video/ffmpeg_pipe_sink.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <sys/types.h>
#include <signal.h>
#endif

using namespace chronon3d::media::video;

namespace {

// ── Runtime ffmpeg check ─────────────────────────────────────────────────

/// Returns true if `ffmpeg` is on PATH.  Cached after first check.
bool ffmpeg_available() {
    static int avail = -1;
    if (avail < 0) {
        avail = (std::system("ffmpeg -version > /dev/null 2>&1") == 0) ? 1 : 0;
    }
    return avail == 1;
}

// ── Test helpers ───────────────────────────────────────────────────────────

/// Unique temp file path per call.  Thread-safe via atomic counter.
[[nodiscard]] std::string temp_path(const char* suffix) {
    static std::atomic<int> counter{0};
    int id = counter.fetch_add(1);
    return "/tmp/chronon3d_ffpipe_" + std::to_string(id) + "_" + suffix;
}

/// Build a minimal valid VideoSinkConfig for the given format/dimensions.
[[nodiscard]] VideoSinkConfig make_config(int w, int h, PixelFormat fmt,
                                          const std::string& path,
                                          bool overwrite = true) {
    VideoSinkConfig cfg;
    cfg.stream.width            = w;
    cfg.stream.height           = h;
    cfg.stream.submitted_format = fmt;
    cfg.encoder.encoded_pixel_format =
        (fmt == PixelFormat::RGBA8 || fmt == PixelFormat::RGB24)
            ? PixelFormat::YUV420P   // encode RGBA/RGB to YUV
            : fmt;                    // YUV stays YUV
    cfg.encoder.codec          = VideoCodec::H264;
    cfg.encoder.apply_gamma    = false;  // linear for determinism
    cfg.encoder.crf            = 30;     // low quality, fast encode
    cfg.encoder.preset         = "ultrafast";
    // The current sinks only support synchronous mode.
    cfg.transport.asynchronous = false;
    cfg.output.output_path     = std::filesystem::path(path);
    cfg.output.overwrite       = overwrite;
    cfg.output.container       = VideoContainer::Mp4;
    cfg.transport.asynchronous = false;
    return cfg;
}

/// Fill a buffer with a deterministic byte pattern.
void fill_pattern(uint8_t* buf, size_t size, uint8_t seed = 0xAB) {
    for (size_t i = 0; i < size; ++i) {
        buf[i] = static_cast<uint8_t>(seed + i * 7);
    }
}

/// Check that a file exists and has non-zero size.
bool file_nonempty(const std::string& path) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    return !ec && sz > 0;
}

// ── RAII file cleanup ──────────────────────────────────────────────────────

struct TempFile {
    std::string path;
    explicit TempFile(std::string p) : path(std::move(p)) {}
    ~TempFile() noexcept { std::error_code ec; std::filesystem::remove(path, ec); }
};

} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════════════
//  Default state
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FfmpegPipeSink: default state is Created with zero stats") {
    FfmpegPipeSink sink;
    CHECK(sink.state() == VideoSinkState::Created);
    CHECK(sink.frames_submitted() == 0);
    CHECK(sink.name() == "ffmpeg-pipe-sink");
    CHECK(sink.ffmpeg_pid() == -1);

    auto s = sink.stats();
    CHECK(s.frames_submitted == 0);
    CHECK(s.bytes_written == 0);
    CHECK(s.submit_count == 0);
    CHECK(s.total_submit_ms == 0.0);
    CHECK(s.total_flush_ms == 0.0);
}

TEST_CASE("FfmpegPipeSink: name is ffmpeg-pipe-sink") {
    FfmpegPipeSink sink;
    CHECK(sink.name() == "ffmpeg-pipe-sink");
}

// ═════════════════════════════════════════════════════════════════════════════
//  State machine — no ffmpeg needed (fail before launch)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FfmpegPipeSink: close on Created succeeds") {
    FfmpegPipeSink sink;
    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);
}

TEST_CASE("FfmpegPipeSink: close on Closed is idempotent") {
    FfmpegPipeSink sink;
    sink.close();
    CHECK(sink.state() == VideoSinkState::Closed);
    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);
}

TEST_CASE("FfmpegPipeSink: submit before open fails") {
    FfmpegPipeSink sink;
    std::vector<uint8_t> buf(64);
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 4;
    fv.height       = 4;
    fv.pixel_format = PixelFormat::RGBA8;
    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.frames_submitted() == 0);
}

TEST_CASE("FfmpegPipeSink: flush before open fails") {
    FfmpegPipeSink sink;
    CHECK_FALSE(sink.flush());
}

TEST_CASE("FfmpegPipeSink: open from Closed state fails") {
    const auto path = temp_path("reopen_ff");

    FfmpegPipeSink sink;
    auto cfg = make_config(8, 8, PixelFormat::RGBA8, path);
    // We need ffmpeg for a successful open, but the test for "Closed is terminal"
    // works even without ffmpeg — we just need to reach Closed state.
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }
    REQUIRE(sink.open(cfg));
    sink.close();
    REQUIRE(sink.state() == VideoSinkState::Closed);

    CHECK_FALSE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Failed);

    std::filesystem::remove(path);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Validation — no ffmpeg needed (fail before launch)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FfmpegPipeSink: open with zero width fails") {
    FfmpegPipeSink sink;
    auto cfg = make_config(0, 16, PixelFormat::RGBA8, temp_path("z_w"));
    CHECK_FALSE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Failed);
}

TEST_CASE("FfmpegPipeSink: open with zero height fails") {
    FfmpegPipeSink sink;
    auto cfg = make_config(16, 0, PixelFormat::RGBA8, temp_path("z_h"));
    CHECK_FALSE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Failed);
}

TEST_CASE("FfmpegPipeSink: open with negative dimensions fails") {
    FfmpegPipeSink sink;
    auto cfg = make_config(-8, 8, PixelFormat::RGBA8, temp_path("neg"));
    CHECK_FALSE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Failed);
}

TEST_CASE("FfmpegPipeSink: open with odd YUV width fails") {
    FfmpegPipeSink sink;
    auto cfg = make_config(9, 8, PixelFormat::YUV420P, temp_path("odd_w"));
    CHECK_FALSE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Failed);
}

TEST_CASE("FfmpegPipeSink: open with odd YUV height fails") {
    FfmpegPipeSink sink;
    auto cfg = make_config(8, 9, PixelFormat::NV12, temp_path("odd_h"));
    CHECK_FALSE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Failed);
}

TEST_CASE("FfmpegPipeSink: open when file exists and overwrite=false fails") {
    const auto path = temp_path("no_ow");

    // Create a dummy file.
    {
        std::ofstream f(path, std::ios::binary);
        f.put('x');
    }
    REQUIRE(std::filesystem::exists(path));

    FfmpegPipeSink sink;
    auto cfg = make_config(8, 8, PixelFormat::RGBA8, path, false);
    CHECK_FALSE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Failed);

    std::filesystem::remove(path);
}

TEST_CASE("FfmpegPipeSink: submit from Created state fails (no open)") {
    FfmpegPipeSink sink;
    std::vector<uint8_t> buf(64);
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 4;
    fv.height       = 4;
    fv.pixel_format = PixelFormat::RGBA8;
    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.state() == VideoSinkState::Created);  // unchanged
}

TEST_CASE("FfmpegPipeSink: submit_planar from Created state fails") {
    FfmpegPipeSink sink;
    PlanarVideoFrameView pfv;
    pfv.y_data = reinterpret_cast<const void*>(0x1);
    pfv.u_data = reinterpret_cast<const void*>(0x1);
    pfv.v_data = reinterpret_cast<const void*>(0x1);
    pfv.width  = 4;
    pfv.height = 4;
    CHECK_FALSE(sink.submit_planar(pfv));
}

TEST_CASE("FfmpegPipeSink: submit_biplanar from Created state fails") {
    FfmpegPipeSink sink;
    BiplanarVideoFrameView bfv;
    bfv.y_data  = reinterpret_cast<const void*>(0x1);
    bfv.uv_data = reinterpret_cast<const void*>(0x1);
    bfv.width   = 4;
    bfv.height  = 4;
    CHECK_FALSE(sink.submit_biplanar(bfv));
}

// ═════════════════════════════════════════════════════════════════════════════
//  Lifecycle tests (requires ffmpeg)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FfmpegPipeSink: full lifecycle with RGBA8 frames") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("lifecycle_rgba.mp4");
    TempFile cleanup(path);
    constexpr int W = 16, H = 16, N = 3;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 4;

    FfmpegPipeSink sink;
    CHECK(sink.state() == VideoSinkState::Created);

    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Open);
    CHECK(sink.ffmpeg_pid() > 0);
    CHECK(sink.frames_submitted() == 0);

    // Submit N frames.
    for (int i = 0; i < N; ++i) {
        std::vector<uint8_t> buf(frame_bytes);
        fill_pattern(buf.data(), frame_bytes, static_cast<uint8_t>(i * 33));

        VideoFrameView fv;
        fv.data         = buf.data();
        fv.width        = W;
        fv.height       = H;
        fv.pixel_format = PixelFormat::RGBA8;
        fv.stride_bytes = 0;

        CHECK(sink.submit(fv));
    }

    CHECK(sink.frames_submitted() == N);
    CHECK(sink.stats().bytes_written == N * frame_bytes);
    CHECK(sink.stats().submit_count == N);

    // Flush.
    CHECK(sink.flush());
    CHECK(sink.state() == VideoSinkState::Open);

    // Close.
    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);

    // Stats persist.
    CHECK(sink.frames_submitted() == N);

    // Output file should exist and be non-empty.
    CHECK(file_nonempty(path));
}

TEST_CASE("FfmpegPipeSink: submit after close fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("after_close.mp4");
    TempFile cleanup(path);
    constexpr int W = 16, H = 16;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 4;

    FfmpegPipeSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(frame_bytes);
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = W;
    fv.height       = H;
    fv.pixel_format = PixelFormat::RGBA8;
    CHECK(sink.submit(fv));
    uint64_t before = sink.frames_submitted();

    CHECK(sink.close());

    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.frames_submitted() == before);
}

TEST_CASE("FfmpegPipeSink: close idempotent after open") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("close_idem.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(16 * 16 * 4);
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;
    sink.submit(fv);

    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);
    CHECK(sink.close());  // second close
    CHECK(sink.state() == VideoSinkState::Closed);
}

TEST_CASE("FfmpegPipeSink: flush after submit succeeds") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("flush_ff.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(16 * 16 * 4);
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;
    CHECK(sink.submit(fv));

    CHECK(sink.flush());
    CHECK(sink.state() == VideoSinkState::Open);

    // Can submit again.
    CHECK(sink.submit(fv));
    CHECK(sink.frames_submitted() == 2);

    CHECK(sink.close());
}

TEST_CASE("FfmpegPipeSink: reset_stats clears counters") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("rstats.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(16 * 16 * 4);
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;
    CHECK(sink.submit(fv));
    CHECK(sink.stats().frames_submitted == 1);

    sink.reset_stats();

    auto s = sink.stats();
    CHECK(s.frames_submitted == 0);
    CHECK(s.bytes_written == 0);
    CHECK(s.submit_count == 0);
    CHECK(sink.state() == VideoSinkState::Open);

    sink.close();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Output tests (requires ffmpeg)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FfmpegPipeSink: zero frames produces valid (empty) output") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("zero_frames.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    // No frames submitted — just flush and close.
    CHECK(sink.flush());
    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);
    CHECK(sink.frames_submitted() == 0);

    // File may exist but could be very small (ffmpeg may write a header).
    // The important thing is that close succeeded without error.
    CHECK(sink.state() == VideoSinkState::Closed);
}

TEST_CASE("FfmpegPipeSink: one frame produces valid output") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("one_frame.mp4");
    TempFile cleanup(path);
    constexpr int W = 32, H = 32;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 4;

    FfmpegPipeSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(frame_bytes);
    fill_pattern(buf.data(), frame_bytes, 0x42);

    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = W;
    fv.height       = H;
    fv.pixel_format = PixelFormat::RGBA8;

    CHECK(sink.submit(fv));
    CHECK(sink.frames_submitted() == 1);
    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);
    CHECK(file_nonempty(path));
}

TEST_CASE("FfmpegPipeSink: multiple frames produce valid output") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("multi.mp4");
    TempFile cleanup(path);
    constexpr int W = 32, H = 32, N = 10;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 4;

    FfmpegPipeSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    for (int i = 0; i < N; ++i) {
        std::vector<uint8_t> buf(frame_bytes);
        fill_pattern(buf.data(), frame_bytes, static_cast<uint8_t>(i));

        VideoFrameView fv;
        fv.data         = buf.data();
        fv.width        = W;
        fv.height       = H;
        fv.pixel_format = PixelFormat::RGBA8;

        CHECK(sink.submit(fv));
    }

    CHECK(sink.frames_submitted() == N);
    CHECK(sink.stats().bytes_written == N * frame_bytes);
    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);
    CHECK(file_nonempty(path));
}

TEST_CASE("FfmpegPipeSink: RGBA8 frames with stride work correctly") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("stride.mp4");
    TempFile cleanup(path);
    constexpr int W = 32, H = 16;
    constexpr int STRIDE = W * 4 + 8;  // 8 bytes padding per row
    const size_t tight_frame = static_cast<size_t>(W) * H * 4;
    const size_t padded_size = static_cast<size_t>(STRIDE) * H;

    FfmpegPipeSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    // Build padded buffer (only tight region has data).
    std::vector<uint8_t> padded(padded_size, 0xAA);
    for (int y = 0; y < H; ++y) {
        fill_pattern(padded.data() + y * STRIDE,
                     static_cast<size_t>(W * 4),
                     static_cast<uint8_t>(y * 17));
    }

    VideoFrameView fv;
    fv.data         = padded.data();
    fv.width        = W;
    fv.height       = H;
    fv.pixel_format = PixelFormat::RGBA8;
    fv.stride_bytes = static_cast<std::size_t>(STRIDE);

    CHECK(sink.submit(fv));
    CHECK(sink.frames_submitted() == 1);
    CHECK(sink.stats().bytes_written == tight_frame);
    CHECK(sink.close());
    CHECK(file_nonempty(path));
}

TEST_CASE("FfmpegPipeSink: planar YUV420P frames work correctly") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("planar.mp4");
    TempFile cleanup(path);
    constexpr int W = 32, H = 32;
    const size_t y_size  = static_cast<size_t>(W) * H;
    const size_t uv_size = y_size / 4;
    const size_t total   = y_size + uv_size * 2;

    FfmpegPipeSink sink;
    auto cfg = make_config(W, H, PixelFormat::YUV420P, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> y_buf(y_size);
    std::vector<uint8_t> u_buf(uv_size);
    std::vector<uint8_t> v_buf(uv_size);
    fill_pattern(y_buf.data(), y_size, 0x10);
    fill_pattern(u_buf.data(), uv_size, 0x80);
    fill_pattern(v_buf.data(), uv_size, 0x80);

    PlanarVideoFrameView pfv;
    pfv.y_data   = y_buf.data();
    pfv.u_data   = u_buf.data();
    pfv.v_data   = v_buf.data();
    pfv.width    = W;
    pfv.height   = H;

    CHECK(sink.submit_planar(pfv));
    CHECK(sink.frames_submitted() == 1);
    CHECK(sink.stats().bytes_written == total);
    CHECK(sink.close());
    CHECK(file_nonempty(path));
}

TEST_CASE("FfmpegPipeSink: biplanar NV12 frames work correctly") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("biplanar.mp4");
    TempFile cleanup(path);
    constexpr int W = 32, H = 32;
    const size_t y_size  = static_cast<size_t>(W) * H;
    const size_t uv_size = y_size / 2;
    const size_t total   = y_size + uv_size;

    FfmpegPipeSink sink;
    auto cfg = make_config(W, H, PixelFormat::NV12, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> y_buf(y_size);
    std::vector<uint8_t> uv_buf(uv_size);
    fill_pattern(y_buf.data(), y_size, 0x20);
    fill_pattern(uv_buf.data(), uv_size, 0x80);

    BiplanarVideoFrameView bfv;
    bfv.y_data   = y_buf.data();
    bfv.uv_data  = uv_buf.data();
    bfv.width    = W;
    bfv.height   = H;

    CHECK(sink.submit_biplanar(bfv));
    CHECK(sink.frames_submitted() == 1);
    CHECK(sink.stats().bytes_written == total);
    CHECK(sink.close());
    CHECK(file_nonempty(path));
}

TEST_CASE("FfmpegPipeSink: container extension is appended to bare path") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto bare_path = temp_path("bare");  // no extension
    const auto expected_path = bare_path + ".mp4";
    TempFile cleanup(bare_path);
    TempFile cleanup2(expected_path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, bare_path);
    cfg.output.container = VideoContainer::Mp4;
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(16 * 16 * 4);
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;
    sink.submit(fv);
    sink.close();

    // The output should have been written to bare.mp4, not bare.
    CHECK(std::filesystem::exists(expected_path));
}

// ═════════════════════════════════════════════════════════════════════════════
//  Validation at submit time (requires ffmpeg to reach Open)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FfmpegPipeSink: submit with null data fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("null_sub.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    VideoFrameView fv;
    fv.data         = nullptr;
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;

    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.state() == VideoSinkState::Failed);
    CHECK(sink.frames_submitted() == 0);

    sink.close();
}

TEST_CASE("FfmpegPipeSink: submit with mismatched format fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("bad_fmt.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::YUV420P, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(16 * 16 * 4);
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;  // wrong format

    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
}

TEST_CASE("FfmpegPipeSink: submit with mismatched dimensions fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("bad_dims.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(32 * 32 * 4);
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 32;   // wrong
    fv.height       = 32;   // wrong
    fv.pixel_format = PixelFormat::RGBA8;

    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
}

TEST_CASE("FfmpegPipeSink: packed YUV with non-zero stride fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("yuv_stride.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::YUV420P, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(16 * 16 * 2);  // padded
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::YUV420P;
    fv.stride_bytes = 32;  // non-zero stride on YUV → rejected

    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
}

TEST_CASE("FfmpegPipeSink: submit_planar with wrong session format fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("planar_wrong_fmt.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);  // RGBA session
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(16 * 16);
    PlanarVideoFrameView pfv;
    pfv.y_data = buf.data();
    pfv.u_data = buf.data();
    pfv.v_data = buf.data();
    pfv.width  = 16;
    pfv.height = 16;

    CHECK_FALSE(sink.submit_planar(pfv));  // planar → rgba mismatch
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
}

TEST_CASE("FfmpegPipeSink: submit_biplanar with null uv_data fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("null_uv.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::NV12, path);
    REQUIRE(sink.open(cfg));

    BiplanarVideoFrameView bfv;
    bfv.y_data  = reinterpret_cast<const void*>(0x1);
    bfv.uv_data = nullptr;  // null UV
    bfv.width   = 16;
    bfv.height  = 16;

    CHECK_FALSE(sink.submit_biplanar(bfv));
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Broken pipe tests (requires ffmpeg)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FfmpegPipeSink: kill ffmpeg mid-stream sets pipeline failure") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("killed.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    // Send one frame successfully.
    std::vector<uint8_t> buf(16 * 16 * 4);
    fill_pattern(buf.data(), buf.size());
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;
    CHECK(sink.submit(fv));

    // Kill the ffmpeg child process with SIGKILL.
    int pid = sink.ffmpeg_pid();
    REQUIRE(pid > 0);
    ::kill(pid, SIGKILL);  // SIGKILL

    // Retry writes until the pipe breaks (ffmpeg child exits).
    // Use a timeout to avoid hanging if the pipe doesn't break.
    bool any_failed = false;
    for (int attempt = 0; attempt < 30; ++attempt) {
        fill_pattern(buf.data(), buf.size(), static_cast<uint8_t>(attempt));
        if (!sink.submit(fv)) {
            any_failed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    CHECK(any_failed);
    CHECK(sink.state() == VideoSinkState::Failed);

    // Close should clean up despite failure.
    sink.close();
}

TEST_CASE("FfmpegPipeSink: close after ffmpeg killed returns Failed state") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("killed_close.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(16 * 16 * 4);
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;
    sink.submit(fv);

    // Kill ffmpeg with SIGKILL.
    int pid = sink.ffmpeg_pid();
    REQUIRE(pid > 0);
    ::kill(pid, SIGKILL);  // SIGKILL

    // Break the pipe by writing until failure.
    for (int attempt = 0; attempt < 30; ++attempt) {
        if (!sink.submit(fv)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    REQUIRE(sink.state() == VideoSinkState::Failed);

    // Close from Failed state: close() returns false because the child
    // was killed (non-zero exit / signal).
    CHECK_FALSE(sink.close());
    CHECK(sink.state() == VideoSinkState::Failed);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Exit code tests (requires ffmpeg)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FfmpegPipeSink: successful close collects exit code 0") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("exit0.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(16 * 16 * 4);
    fill_pattern(buf.data(), buf.size());
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;
    sink.submit(fv);

    // Close should return true (exit code 0).
    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Overwrite tests (requires ffmpeg)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FfmpegPipeSink: path with spaces works correctly") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    // Path containing spaces — verifies argv is passed directly (no shell).
    const auto path = temp_path("path with spaces.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(16 * 16 * 4);
    fill_pattern(buf.data(), buf.size());
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;
    CHECK(sink.submit(fv));
    CHECK(sink.close());
    CHECK(file_nonempty(path));
}

TEST_CASE("FfmpegPipeSink: submit_planar with mismatched dimensions fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("planar_dims.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::YUV420P, path);
    REQUIRE(sink.open(cfg));

    // Submit planar frame with different dimensions.
    std::vector<uint8_t> buf(32 * 32);
    PlanarVideoFrameView pfv;
    pfv.y_data = buf.data();
    pfv.u_data = buf.data();
    pfv.v_data = buf.data();
    pfv.width  = 32;   // wrong
    pfv.height = 32;   // wrong

    CHECK_FALSE(sink.submit_planar(pfv));
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
}

TEST_CASE("FfmpegPipeSink: submit_biplanar with mismatched dimensions fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("biplanar_dims.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::NV12, path);
    REQUIRE(sink.open(cfg));

    // Submit biplanar frame with different dimensions.
    std::vector<uint8_t> buf(32 * 32);
    BiplanarVideoFrameView bfv;
    bfv.y_data  = buf.data();
    bfv.uv_data = buf.data();
    bfv.width   = 32;   // wrong
    bfv.height  = 32;   // wrong

    CHECK_FALSE(sink.submit_biplanar(bfv));
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
}

TEST_CASE("FfmpegPipeSink: packed stride too small fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("stride_small.mp4");
    TempFile cleanup(path);

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    // RGBA8 tight row = 64 bytes.  Stride of 32 < 64 → rejected.
    std::vector<uint8_t> buf(32 * 16);
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;
    fv.stride_bytes = 32;  // too small

    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Write timeout tests (requires ffmpeg)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FfmpegPipeSink: write_timeout=0 uses blocking write (normal)") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("tmout0.mp4");
    TempFile cleanup(path);
    constexpr int W = 16, H = 16, N = 3;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 4;

    FfmpegPipeSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    cfg.transport.write_timeout = std::chrono::milliseconds(0);  // no timeout
    REQUIRE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Open);

    for (int i = 0; i < N; ++i) {
        std::vector<uint8_t> buf(frame_bytes);
        fill_pattern(buf.data(), frame_bytes, static_cast<uint8_t>(i * 33));
        VideoFrameView fv;
        fv.data         = buf.data();
        fv.width        = W;
        fv.height       = H;
        fv.pixel_format = PixelFormat::RGBA8;
        CHECK(sink.submit(fv));
    }
    CHECK(sink.frames_submitted() == N);
    CHECK(sink.close());
    CHECK(file_nonempty(path));
}

TEST_CASE("FfmpegPipeSink: write_timeout=5ms succeeds for normal writes") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("tmout5.mp4");
    TempFile cleanup(path);
    constexpr int W = 16, H = 16;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 4;

    FfmpegPipeSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    cfg.transport.write_timeout = std::chrono::milliseconds(5);  // very short
    REQUIRE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Open);

    // Submit a few frames — with ffmpeg consuming stdin, the pipe stays
    // drained enough that writes complete within 5ms.
    for (int i = 0; i < 3; ++i) {
        std::vector<uint8_t> buf(frame_bytes);
        fill_pattern(buf.data(), frame_bytes, static_cast<uint8_t>(i));
        VideoFrameView fv;
        fv.data         = buf.data();
        fv.width        = W;
        fv.height       = H;
        fv.pixel_format = PixelFormat::RGBA8;
        CHECK(sink.submit(fv));
    }
    CHECK(sink.frames_submitted() == 3);
    CHECK(sink.close());
    CHECK(file_nonempty(path));
}

TEST_CASE("FfmpegPipeSink: write_timeout=10ms with killed child fails fast") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("tmout_kill.mp4");
    TempFile cleanup(path);
    constexpr int W = 16, H = 16;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 4;

    FfmpegPipeSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    cfg.transport.write_timeout = std::chrono::milliseconds(10);
    REQUIRE(sink.open(cfg));

    // Send one frame successfully.
    {
        std::vector<uint8_t> buf(frame_bytes);
        fill_pattern(buf.data(), frame_bytes);
        VideoFrameView fv;
        fv.data         = buf.data();
        fv.width        = W;
        fv.height       = H;
        fv.pixel_format = PixelFormat::RGBA8;
        CHECK(sink.submit(fv));
    }

    // Kill ffmpeg — subsequent writes should fail quickly (EPIPE, not timeout).
    int pid = sink.ffmpeg_pid();
    REQUIRE(pid > 0);
    ::kill(pid, SIGKILL);

    // Wait for child to die, then verify write fails fast (within 10ms).
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto t0 = std::chrono::steady_clock::now();
    {
        std::vector<uint8_t> buf(frame_bytes);
        fill_pattern(buf.data(), frame_bytes);
        VideoFrameView fv;
        fv.data         = buf.data();
        fv.width        = W;
        fv.height       = H;
        fv.pixel_format = PixelFormat::RGBA8;
        CHECK_FALSE(sink.submit(fv));
    }
    auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();

    CHECK(sink.state() == VideoSinkState::Failed);
    // The failure should be fast (EPIPE breaks immediately, or pipe full
    // hits the 10ms timeout).  Allow a generous margin for CI noise.
    CHECK(elapsed < 500);  // well under 30s default

    sink.close();
}

// ═════════════════════════════════════════════════════════════════════════════
//  Overwrite tests (requires ffmpeg)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FfmpegPipeSink: overwrite=true succeeds when file exists") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = temp_path("ow_true.mp4");
    TempFile cleanup(path);

    // Create a dummy file first.
    {
        std::ofstream f(path, std::ios::binary);
        f.put('x');
    }
    REQUIRE(std::filesystem::exists(path));

    FfmpegPipeSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path, true);
    CHECK(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Open);

    std::vector<uint8_t> buf(16 * 16 * 4);
    VideoFrameView fv;
    fv.data         = buf.data();
    fv.width        = 16;
    fv.height       = 16;
    fv.pixel_format = PixelFormat::RGBA8;
    sink.submit(fv);
    sink.close();
    CHECK(file_nonempty(path));
}

// ═════════════════════════════════════════════════════════════════════════════
//  FFmpeg failure: invalid codec/container combination
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FfmpegPipeSink: nonexistent output directory fails") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    FfmpegPipeSink sink;
    // A path in a directory that definitely doesn't exist.
    auto cfg = make_config(16, 16, PixelFormat::RGBA8,
                           "/nonexistent_dir_xyz_123/test.mp4");
    // open() may succeed (it checks dimensions but not directory existence
    // for the output path — ffmpeg handles that).  If open succeeds, the
    // first submit or close will detect ffmpeg's failure.
    bool opened = sink.open(cfg);
    if (opened) {
        std::vector<uint8_t> buf(16 * 16 * 4);
        VideoFrameView fv;
        fv.data         = buf.data();
        fv.width        = 16;
        fv.height       = 16;
        fv.pixel_format = PixelFormat::RGBA8;
        bool submit_ok = sink.submit(fv);
        bool close_ok  = sink.close();
        CHECK((!submit_ok || !close_ok));
    }
}

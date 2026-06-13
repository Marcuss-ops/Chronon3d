// ---------------------------------------------------------------------------
// test_raw_video_sink.cpp — Unit tests for RawVideoSink lifecycle.
//
// Tests the VideoSink contract for RawVideoSink:
//   - Default state (Created, zero stats)
//   - open() with valid/invalid config → state transitions
//   - submit() with RGBA8 frame data → file size, counters
//   - submit_planar() packs YUV420P planes into tight interleaved buffer
//   - submit_biplanar() packs NV12 planes
//   - flush() transitions to Flushing
//   - close() idempotent, transitions to Closed
//   - submit() after close() fails
//   - Destructor does not throw
//   - Stats tracking (frames_submitted, bytes_written, timing)
//   - All 4 pixel formats: RGBA8, YUV420P, NV12, RGB24
//
// Dependencies: chronon3d_media_video (header + source), doctest.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <chronon3d/media/video/video_sink.hpp>
#include <chronon3d/media/video/video_frame.hpp>
#include <chronon3d/media/video/video_config.hpp>
#include "src/media/video/raw_video_sink.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace chronon3d::media::video;

namespace {

// ── Test helpers ───────────────────────────────────────────────────────────

/// Unique temp file path per call.  Thread-safe via atomic counter.
[[nodiscard]] std::string temp_path(const char* suffix) {
    static std::atomic<int> counter{0};
    int id = counter.fetch_add(1);
    return "/tmp/chronon3d_raw_sink_" + std::to_string(id) + "_" + suffix + ".raw";
}

/// Build a minimal valid VideoSinkConfig for the given format/dimensions.
[[nodiscard]] VideoSinkConfig make_config(int w, int h, PixelFormat fmt,
                                          const std::string& path,
                                          bool overwrite = true) {
    VideoSinkConfig cfg;
    cfg.stream.width  = w;
    cfg.stream.height = h;
    cfg.stream.input_format = fmt;
    cfg.encoder.output_format = fmt;
    cfg.encoder.apply_gamma = false;  // linear for determinism
    cfg.output.output_path = std::filesystem::path(path);
    cfg.output.overwrite = overwrite;
    return cfg;
}

/// Fill a buffer with a deterministic byte pattern for validation.
void fill_pattern(uint8_t* buf, size_t size, uint8_t seed = 0xAB) {
    for (size_t i = 0; i < size; ++i) {
        buf[i] = static_cast<uint8_t>(seed + i * 7);
    }
}

/// Check that a file contains exactly the expected number of bytes.
bool file_has_size(const std::string& path, size_t expected) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    return !ec && sz == expected;
}

} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════════════
//  Default state
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("RawVideoSink: default state is Created with zero stats") {
    RawVideoSink sink;
    CHECK(sink.state() == VideoSinkState::Created);
    CHECK(sink.frames_submitted() == 0);
    CHECK(sink.name() == "raw-video-sink");

    auto s = sink.stats();
    CHECK(s.frames_submitted == 0);
    CHECK(s.bytes_written == 0);
    CHECK(s.submit_count == 0);
    CHECK(s.total_submit_ms == 0.0);
    CHECK(s.total_flush_ms == 0.0);
}

TEST_CASE("RawVideoSink: name is raw-video-sink") {
    RawVideoSink sink;
    CHECK(sink.name() == "raw-video-sink");
}

// ═════════════════════════════════════════════════════════════════════════════
//  open() — lifecycle
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("RawVideoSink: open creates output file") {
    const auto path = temp_path("open_creates");

    RawVideoSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path);
    CHECK(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Open);
    CHECK(std::filesystem::exists(path));
    CHECK(sink.frames_submitted() == 0);

    sink.close();
    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: open fails on invalid dimensions") {
    const auto path = temp_path("open_bad_dims");

    RawVideoSink sink;
    auto cfg = make_config(0, 16, PixelFormat::RGBA8, path);
    CHECK_FALSE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Failed);

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: open fails on invalid directory") {
    RawVideoSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8,
                           "/nonexistent_dir_xyz/test.raw");
    CHECK_FALSE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Failed);
}

TEST_CASE("RawVideoSink: open fails when file exists and overwrite=false") {
    const auto path = temp_path("open_no_overwrite");

    // Create file first
    {
        std::ofstream f(path, std::ios::binary);
        f.put('x');
    }
    CHECK(std::filesystem::exists(path));

    RawVideoSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path, false);
    CHECK_FALSE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Failed);

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: open succeeds when file exists and overwrite=true") {
    const auto path = temp_path("open_overwrite");

    // Create file first
    {
        std::ofstream f(path, std::ios::binary);
        f.put('x');
    }

    RawVideoSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8, path, true);
    CHECK(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Open);

    sink.close();
    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: open from Closed state reopens successfully") {
    const auto path = temp_path("reopen");

    RawVideoSink sink;
    {
        auto cfg = make_config(8, 8, PixelFormat::RGBA8, path);
        REQUIRE(sink.open(cfg));
        sink.close();
        CHECK(sink.state() == VideoSinkState::Closed);
    }

    // Reopen
    const auto path2 = temp_path("reopen2");
    auto cfg2 = make_config(16, 16, PixelFormat::RGBA8, path2);
    CHECK(sink.open(cfg2));
    CHECK(sink.state() == VideoSinkState::Open);

    sink.close();
    std::filesystem::remove(path);
    std::filesystem::remove(path2);
}

// ═════════════════════════════════════════════════════════════════════════════
//  submit() — RGBA8
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("RawVideoSink: submit one RGBA8 frame") {
    const auto path = temp_path("submit_rgba8_1");
    constexpr int W = 8, H = 8;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 4;

    RawVideoSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(frame_bytes);
    fill_pattern(buf.data(), frame_bytes, 0x10);

    VideoFrameView fv;
    fv.data          = buf.data();
    fv.width         = W;
    fv.height        = H;
    fv.pixel_format  = PixelFormat::RGBA8;
    fv.stride_bytes  = 0;  // tight

    CHECK(sink.submit(fv));
    CHECK(sink.frames_submitted() == 1);
    CHECK(sink.stats().bytes_written == frame_bytes);
    CHECK(sink.stats().submit_count == 1);

    sink.close();

    CHECK(file_has_size(path, frame_bytes));
    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: submit multiple RGBA8 frames") {
    const auto path = temp_path("submit_rgba8_multi");
    constexpr int W = 8, H = 8, N = 5;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 4;

    RawVideoSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    for (int i = 0; i < N; ++i) {
        std::vector<uint8_t> buf(frame_bytes);
        fill_pattern(buf.data(), frame_bytes, static_cast<uint8_t>(i * 16));

        VideoFrameView fv;
        fv.data          = buf.data();
        fv.width         = W;
        fv.height        = H;
        fv.pixel_format  = PixelFormat::RGBA8;
        fv.stride_bytes  = 0;

        CHECK(sink.submit(fv));
    }

    CHECK(sink.frames_submitted() == N);
    CHECK(sink.stats().bytes_written == N * frame_bytes);
    CHECK(sink.stats().submit_count == N);

    sink.close();
    CHECK(file_has_size(path, N * frame_bytes));
    std::filesystem::remove(path);
}

// ═════════════════════════════════════════════════════════════════════════════
//  submit() — YUV420P and NV12 packed
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("RawVideoSink: submit YUV420P packed frame") {
    const auto path = temp_path("submit_yuv420p");
    constexpr int W = 8, H = 8;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 3 / 2;

    RawVideoSink sink;
    auto cfg = make_config(W, H, PixelFormat::YUV420P, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(frame_bytes);
    fill_pattern(buf.data(), frame_bytes, 0x20);

    VideoFrameView fv;
    fv.data          = buf.data();
    fv.width         = W;
    fv.height        = H;
    fv.pixel_format  = PixelFormat::YUV420P;
    fv.stride_bytes  = 0;

    CHECK(sink.submit(fv));
    CHECK(sink.frames_submitted() == 1);

    sink.close();
    CHECK(file_has_size(path, frame_bytes));
    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: submit NV12 packed frame") {
    const auto path = temp_path("submit_nv12");
    constexpr int W = 8, H = 8;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 3 / 2;

    RawVideoSink sink;
    auto cfg = make_config(W, H, PixelFormat::NV12, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(frame_bytes);
    fill_pattern(buf.data(), frame_bytes, 0x30);

    VideoFrameView fv;
    fv.data          = buf.data();
    fv.width         = W;
    fv.height        = H;
    fv.pixel_format  = PixelFormat::NV12;
    fv.stride_bytes  = 0;

    CHECK(sink.submit(fv));
    CHECK(sink.frames_submitted() == 1);

    sink.close();
    CHECK(file_has_size(path, frame_bytes));
    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: submit RGB24 packed frame") {
    const auto path = temp_path("submit_rgb24");
    constexpr int W = 8, H = 8;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 3;

    RawVideoSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGB24, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(frame_bytes);
    fill_pattern(buf.data(), frame_bytes, 0x40);

    VideoFrameView fv;
    fv.data          = buf.data();
    fv.width         = W;
    fv.height        = H;
    fv.pixel_format  = PixelFormat::RGB24;
    fv.stride_bytes  = 0;

    CHECK(sink.submit(fv));
    CHECK(sink.frames_submitted() == 1);

    sink.close();
    CHECK(file_has_size(path, frame_bytes));
    std::filesystem::remove(path);
}

// ═════════════════════════════════════════════════════════════════════════════
//  submit() — data validation (RGBA8 content roundtrip)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("RawVideoSink: written bytes match input exactly") {
    const auto path = temp_path("exact_bytes");
    constexpr int W = 4, H = 4;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 4;

    RawVideoSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    // Fill with deterministic data
    std::vector<uint8_t> expected(frame_bytes);
    for (size_t i = 0; i < frame_bytes; ++i) {
        expected[i] = static_cast<uint8_t>(i * 13 + 0x77);
    }

    VideoFrameView fv;
    fv.data          = expected.data();
    fv.width         = W;
    fv.height        = H;
    fv.pixel_format  = PixelFormat::RGBA8;
    fv.stride_bytes  = 0;

    CHECK(sink.submit(fv));
    sink.close();

    // Read file back and compare
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> actual(frame_bytes);
    file.read(reinterpret_cast<char*>(actual.data()),
              static_cast<std::streamsize>(frame_bytes));
    CHECK(file.gcount() == static_cast<std::streamsize>(frame_bytes));

    CHECK(std::memcmp(expected.data(), actual.data(), frame_bytes) == 0);

    std::filesystem::remove(path);
}

// ═════════════════════════════════════════════════════════════════════════════
//  submit_planar() — YUV420P with tight and non-zero stride
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("RawVideoSink: submit_planar with tight stride (stride=0)") {
    const auto path = temp_path("planar_tight");
    constexpr int W = 8, H = 8;
    const size_t y_size  = static_cast<size_t>(W) * H;
    const size_t uv_size = y_size / 4;
    const size_t total   = y_size + uv_size * 2;

    RawVideoSink sink;
    auto cfg = make_config(W, H, PixelFormat::YUV420P, path);
    REQUIRE(sink.open(cfg));

    // Allocate and fill 3 separate planes
    std::vector<uint8_t> y_buf(y_size);
    std::vector<uint8_t> u_buf(uv_size);
    std::vector<uint8_t> v_buf(uv_size);
    fill_pattern(y_buf.data(), y_size, 0xA0);
    fill_pattern(u_buf.data(), uv_size, 0xB0);
    fill_pattern(v_buf.data(), uv_size, 0xC0);

    PlanarVideoFrameView pfv;
    pfv.y_data   = y_buf.data();
    pfv.u_data   = u_buf.data();
    pfv.v_data   = v_buf.data();
    pfv.y_stride = 0;
    pfv.u_stride = 0;
    pfv.v_stride = 0;
    pfv.width    = W;
    pfv.height   = H;

    CHECK(sink.submit_planar(pfv));
    CHECK(sink.frames_submitted() == 1);
    CHECK(sink.stats().bytes_written == total);

    sink.close();
    CHECK(file_has_size(path, total));

    // Verify content: Y followed by U followed by V
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> file_buf(total);
    file.read(reinterpret_cast<char*>(file_buf.data()),
              static_cast<std::streamsize>(total));

    CHECK(std::memcmp(file_buf.data(),           y_buf.data(), y_size) == 0);
    CHECK(std::memcmp(file_buf.data() + y_size,   u_buf.data(), uv_size) == 0);
    CHECK(std::memcmp(file_buf.data() + y_size + uv_size, v_buf.data(), uv_size) == 0);

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: submit_planar with non-zero stride") {
    const auto path = temp_path("planar_stride");
    constexpr int W = 8, H = 8;
    constexpr int Y_STRIDE = 16;  // 8 bytes padding per row
    constexpr int UV_STRIDE = 10; // 6 bytes padding per row
    const size_t y_size  = static_cast<size_t>(W) * H;
    const size_t uv_size = y_size / 4;
    const size_t total   = y_size + uv_size * 2;

    RawVideoSink sink;
    auto cfg = make_config(W, H, PixelFormat::YUV420P, path);
    REQUIRE(sink.open(cfg));

    // Allocate planes with padding rows
    std::vector<uint8_t> y_padded(static_cast<size_t>(Y_STRIDE) * H);
    std::vector<uint8_t> u_padded(static_cast<size_t>(UV_STRIDE) * (H / 2));
    std::vector<uint8_t> v_padded(static_cast<size_t>(UV_STRIDE) * (H / 2));
    fill_pattern(y_padded.data(), y_padded.size(), 0x10);
    fill_pattern(u_padded.data(), u_padded.size(), 0x20);
    fill_pattern(v_padded.data(), v_padded.size(), 0x30);

    PlanarVideoFrameView pfv;
    pfv.y_data   = y_padded.data();
    pfv.u_data   = u_padded.data();
    pfv.v_data   = v_padded.data();
    pfv.y_stride = static_cast<std::size_t>(Y_STRIDE);
    pfv.u_stride = static_cast<std::size_t>(UV_STRIDE);
    pfv.v_stride = static_cast<std::size_t>(UV_STRIDE);
    pfv.width    = W;
    pfv.height   = H;

    CHECK(sink.submit_planar(pfv));
    CHECK(sink.frames_submitted() == 1);

    sink.close();

    // Verify: the file should contain only the tight pixels (stride-removed)
    // For each row of Y, only the first W bytes are copied.
    // For each row of U/V row, only the first W/2 bytes are copied.
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> file_buf(total);
    file.read(reinterpret_cast<char*>(file_buf.data()),
              static_cast<std::streamsize>(total));

    // Check Y: per-row src = y_padded[row * Y_STRIDE], copied = W bytes
    for (int y = 0; y < H; ++y) {
        const uint8_t* src = y_padded.data() + y * Y_STRIDE;
        const uint8_t* dst = file_buf.data() + y * W;
        CHECK(std::memcmp(dst, src, W) == 0);
    }

    // Check U: per-row src = u_padded[row * UV_STRIDE], copied = W/2 bytes
    for (int y = 0; y < H / 2; ++y) {
        const uint8_t* src = u_padded.data() + y * UV_STRIDE;
        const uint8_t* dst = file_buf.data() + y_size + y * (W / 2);
        CHECK(std::memcmp(dst, src, W / 2) == 0);
    }

    // Check V
    for (int y = 0; y < H / 2; ++y) {
        const uint8_t* src = v_padded.data() + y * UV_STRIDE;
        const uint8_t* dst = file_buf.data() + y_size + uv_size + y * (W / 2);
        CHECK(std::memcmp(dst, src, W / 2) == 0);
    }

    std::filesystem::remove(path);
}

// ═════════════════════════════════════════════════════════════════════════════
//  submit_biplanar() — NV12 with tight and non-zero stride
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("RawVideoSink: submit_biplanar with tight stride") {
    const auto path = temp_path("biplanar_tight");
    constexpr int W = 8, H = 8;
    const size_t y_size  = static_cast<size_t>(W) * H;
    const size_t uv_size = y_size / 2;
    const size_t total   = y_size + uv_size;

    RawVideoSink sink;
    auto cfg = make_config(W, H, PixelFormat::NV12, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> y_buf(y_size);
    std::vector<uint8_t> uv_buf(uv_size);
    fill_pattern(y_buf.data(), y_size, 0x50);
    fill_pattern(uv_buf.data(), uv_size, 0x60);

    BiplanarVideoFrameView bfv;
    bfv.y_data   = y_buf.data();
    bfv.uv_data  = uv_buf.data();
    bfv.y_stride = 0;
    bfv.uv_stride = 0;
    bfv.width    = W;
    bfv.height   = H;

    CHECK(sink.submit_biplanar(bfv));
    CHECK(sink.frames_submitted() == 1);

    sink.close();
    CHECK(file_has_size(path, total));

    // Verify content
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> file_buf(total);
    file.read(reinterpret_cast<char*>(file_buf.data()),
              static_cast<std::streamsize>(total));

    CHECK(std::memcmp(file_buf.data(),         y_buf.data(), y_size) == 0);
    CHECK(std::memcmp(file_buf.data() + y_size, uv_buf.data(), uv_size) == 0);

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: submit_biplanar with non-zero stride") {
    const auto path = temp_path("biplanar_stride");
    constexpr int W = 8, H = 8;
    constexpr int Y_STRIDE  = 16;
    constexpr int UV_STRIDE = 16;
    const size_t y_size  = static_cast<size_t>(W) * H;
    const size_t uv_size = y_size / 2;
    const size_t total   = y_size + uv_size;

    RawVideoSink sink;
    auto cfg = make_config(W, H, PixelFormat::NV12, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> y_padded(static_cast<size_t>(Y_STRIDE) * H);
    std::vector<uint8_t> uv_padded(static_cast<size_t>(UV_STRIDE) * (H / 2));
    fill_pattern(y_padded.data(), y_padded.size(), 0x70);
    fill_pattern(uv_padded.data(), uv_padded.size(), 0x80);

    BiplanarVideoFrameView bfv;
    bfv.y_data    = y_padded.data();
    bfv.uv_data   = uv_padded.data();
    bfv.y_stride  = static_cast<std::size_t>(Y_STRIDE);
    bfv.uv_stride = static_cast<std::size_t>(UV_STRIDE);
    bfv.width     = W;
    bfv.height    = H;

    CHECK(sink.submit_biplanar(bfv));
    CHECK(sink.frames_submitted() == 1);

    sink.close();

    // Verify tight content
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> file_buf(total);
    file.read(reinterpret_cast<char*>(file_buf.data()),
              static_cast<std::streamsize>(total));

    for (int y = 0; y < H; ++y) {
        const uint8_t* src = y_padded.data() + y * Y_STRIDE;
        const uint8_t* dst = file_buf.data() + y * W;
        CHECK(std::memcmp(dst, src, W) == 0);
    }
    for (int y = 0; y < H / 2; ++y) {
        const uint8_t* src = uv_padded.data() + y * UV_STRIDE;
        const uint8_t* dst = file_buf.data() + y_size + y * W;
        CHECK(std::memcmp(dst, src, W) == 0);
    }

    std::filesystem::remove(path);
}

// ═════════════════════════════════════════════════════════════════════════════
//  flush() / close()
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("RawVideoSink: flush on open sink succeeds") {
    const auto path = temp_path("flush_ok");
    RawVideoSink sink;
    auto cfg = make_config(8, 8, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    CHECK(sink.flush());
    CHECK(sink.state() == VideoSinkState::Flushing);

    // Can still close after flush
    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: flush on closed sink fails") {
    const auto path = temp_path("flush_closed");
    RawVideoSink sink;
    auto cfg = make_config(8, 8, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));
    sink.close();

    CHECK_FALSE(sink.flush());

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: close on created sink is idempotent") {
    RawVideoSink sink;
    CHECK(sink.close());  // no-op
    CHECK(sink.state() == VideoSinkState::Closed);
}

TEST_CASE("RawVideoSink: close on closed sink is idempotent") {
    const auto path = temp_path("close_idempotent");
    RawVideoSink sink;
    auto cfg = make_config(8, 8, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));
    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);

    // Second close
    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: close flushes data to disk") {
    const auto path = temp_path("close_flush_data");
    constexpr int W = 8, H = 8;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 4;

    RawVideoSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(frame_bytes);
    fill_pattern(buf.data(), frame_bytes);

    VideoFrameView fv;
    fv.data          = buf.data();
    fv.width         = W;
    fv.height        = H;
    fv.pixel_format  = PixelFormat::RGBA8;
    fv.stride_bytes  = 0;
    CHECK(sink.submit(fv));

    CHECK(sink.close());
    CHECK(std::filesystem::exists(path));
    CHECK(std::filesystem::file_size(path) == frame_bytes);

    std::filesystem::remove(path);
}

// ═════════════════════════════════════════════════════════════════════════════
//  submit() after close() fails
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("RawVideoSink: submit after close fails") {
    const auto path = temp_path("submit_after_close");
    constexpr int W = 8, H = 8;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 4;

    RawVideoSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(frame_bytes);
    VideoFrameView fv;
    fv.data          = buf.data();
    fv.width         = W;
    fv.height        = H;
    fv.pixel_format  = PixelFormat::RGBA8;
    fv.stride_bytes  = 0;

    CHECK(sink.submit(fv));
    uint64_t before = sink.frames_submitted();

    CHECK(sink.close());

    // submit after close must fail
    bool wrote = sink.submit(fv);
    CHECK_FALSE(wrote);
    CHECK(sink.frames_submitted() == before);

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: submit_planar after close fails") {
    const auto path = temp_path("planar_after_close");
    constexpr int W = 4, H = 4;
    const size_t y_sz = static_cast<size_t>(W) * H;
    const size_t uv_sz = y_sz / 4;

    RawVideoSink sink;
    auto cfg = make_config(W, H, PixelFormat::YUV420P, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> y_buf_p(y_sz, 0xFF);
    std::vector<uint8_t> u_buf_p(uv_sz, 0x80);
    std::vector<uint8_t> v_buf_p(uv_sz, 0x40);

    PlanarVideoFrameView pfv;
    pfv.y_data   = y_buf_p.data();
    pfv.u_data   = u_buf_p.data();
    pfv.v_data   = v_buf_p.data();
    pfv.width    = W;
    pfv.height   = H;
    pfv.y_stride = 0;
    pfv.u_stride = 0;
    pfv.v_stride = 0;

    CHECK(sink.submit_planar(pfv));
    uint64_t before = sink.frames_submitted();

    CHECK(sink.close());

    bool wrote = sink.submit_planar(pfv);
    CHECK_FALSE(wrote);
    CHECK(sink.frames_submitted() == before);

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: submit_biplanar after close fails") {
    const auto path = temp_path("biplanar_after_close");
    constexpr int W = 4, H = 4;
    const size_t y_sz_b = static_cast<size_t>(W) * H;
    const size_t uv_sz_b = y_sz_b / 2;

    RawVideoSink sink;
    auto cfg = make_config(W, H, PixelFormat::NV12, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> y_buf_b(y_sz_b, 0xFF);
    std::vector<uint8_t> uv_buf_b(uv_sz_b, 0x80);

    BiplanarVideoFrameView bfv;
    bfv.y_data    = y_buf_b.data();
    bfv.uv_data   = uv_buf_b.data();
    bfv.width     = W;
    bfv.height    = H;
    bfv.y_stride  = 0;
    bfv.uv_stride = 0;

    CHECK(sink.submit_biplanar(bfv));
    uint64_t before = sink.frames_submitted();

    CHECK(sink.close());

    bool wrote = sink.submit_biplanar(bfv);
    CHECK_FALSE(wrote);
    CHECK(sink.frames_submitted() == before);

    std::filesystem::remove(path);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Destructor — no throw
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("RawVideoSink: destructor closes open sink without throwing") {
    const auto path = temp_path("dtor_close");
    {
        RawVideoSink sink;
        auto cfg = make_config(8, 8, PixelFormat::RGBA8, path);
        REQUIRE(sink.open(cfg));

        // Write one frame
        std::vector<uint8_t> buf(8 * 8 * 4);
        VideoFrameView fv;
        fv.data          = buf.data();
        fv.width         = 8;
        fv.height        = 8;
        fv.pixel_format  = PixelFormat::RGBA8;
        fv.stride_bytes  = 0;
        CHECK(sink.submit(fv));

        // sink destroyed here — must not throw
    }
    // File should still exist with data
    CHECK(std::filesystem::exists(path));
    CHECK(std::filesystem::file_size(path) == 8 * 8 * 4);

    std::filesystem::remove(path);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Stats tracking
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("RawVideoSink: stats accumulate across multiple submits") {
    const auto path = temp_path("stats_accum");
    constexpr int W = 4, H = 4, N = 3;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 4;

    RawVideoSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    // Write N frames
    for (int i = 0; i < N; ++i) {
        std::vector<uint8_t> buf(frame_bytes);
        VideoFrameView fv;
        fv.data          = buf.data();
        fv.width         = W;
        fv.height        = H;
        fv.pixel_format  = PixelFormat::RGBA8;
        fv.stride_bytes  = 0;
        CHECK(sink.submit(fv));
    }

    auto s = sink.stats();
    CHECK(s.frames_submitted == N);
    CHECK(s.bytes_written == N * frame_bytes);
    CHECK(s.submit_count == N);
    CHECK(s.total_submit_ms > 0.0);

    // total_flush_ms should be 0 until flush is called
    CHECK(s.total_flush_ms == 0.0);

    sink.close();

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: reset_stats clears counters without changing state") {
    const auto path = temp_path("reset_stats");
    RawVideoSink sink;
    auto cfg = make_config(8, 8, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(8 * 8 * 4);
    VideoFrameView fv;
    fv.data          = buf.data();
    fv.width         = 8;
    fv.height        = 8;
    fv.pixel_format  = PixelFormat::RGBA8;
    fv.stride_bytes  = 0;
    CHECK(sink.submit(fv));

    CHECK(sink.stats().frames_submitted == 1);

    sink.reset_stats();

    auto s = sink.stats();
    CHECK(s.frames_submitted == 0);
    CHECK(s.bytes_written == 0);
    CHECK(s.submit_count == 0);
    CHECK(s.total_submit_ms == 0.0);

    // State should still be Open
    CHECK(sink.state() == VideoSinkState::Open);

    sink.close();
    std::filesystem::remove(path);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Full lifecycle
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("RawVideoSink: full lifecycle open->submit->flush->close") {
    const auto path = temp_path("full_lifecycle");
    constexpr int W = 8, H = 8, N = 3;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 4;

    // Created
    RawVideoSink sink;
    CHECK(sink.state() == VideoSinkState::Created);

    // Open
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    CHECK(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Open);

    // Submit N frames
    for (int i = 0; i < N; ++i) {
        std::vector<uint8_t> buf(frame_bytes);
        fill_pattern(buf.data(), frame_bytes, static_cast<uint8_t>(i * 33));
        VideoFrameView fv;
        fv.data          = buf.data();
        fv.width         = W;
        fv.height        = H;
        fv.pixel_format  = PixelFormat::RGBA8;
        fv.stride_bytes  = 0;
        CHECK(sink.submit(fv));
    }
    CHECK(sink.frames_submitted() == N);

    // Flush
    CHECK(sink.flush());
    CHECK(sink.state() == VideoSinkState::Flushing);

    // Close
    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);

    // Verify output file
    CHECK(file_has_size(path, N * frame_bytes));

    std::filesystem::remove(path);
}

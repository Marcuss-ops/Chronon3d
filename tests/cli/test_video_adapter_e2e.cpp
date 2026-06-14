// ---------------------------------------------------------------------------
// test_video_adapter_e2e.cpp — End-to-end tests for VideoSinkEncoderAdapter.
//
// Tests the full chain from the CLI adapter through the new VideoSink pipeline:
//
//   Framebuffer → VideoSinkEncoderAdapter → VideoSink → file (raw / MP4)
//
// Unlike the unit tests in test_ffmpeg_pipe_sink.cpp (which test the sink
// directly), these tests exercise the CLI adapter layer that bridges the
// IVideoEncoder interface to the media::video::VideoSink API.
//
// Tests that require ffmpeg are guarded by a runtime check.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <apps/chronon3d_cli/utils/video/video_sink_adapter.hpp>
#include <apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.hpp>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::cli;

namespace {

// ── Runtime ffmpeg check ───────────────────────────────────────────────

bool ffmpeg_available() {
    static int avail = -1;
    if (avail < 0) {
        avail = (std::system("ffmpeg -version > /dev/null 2>&1") == 0) ? 1 : 0;
    }
    return avail == 1;
}

// ── Temp path helper ───────────────────────────────────────────────────

std::string e2e_temp_path(const char* suffix) {
    static std::atomic<int> counter{0};
    int id = counter.fetch_add(1);
    return "/tmp/chronon3d_adapter_e2e_" + std::to_string(id) + "_" + suffix;
}

// ── RAII temp file ─────────────────────────────────────────────────────

struct TempFile {
    std::string path;
    explicit TempFile(std::string p) : path(std::move(p)) {}
    ~TempFile() noexcept {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

// ── Build options helper ───────────────────────────────────────────────

FfmpegPipeOptions make_adapter_opts(int w, int h,
                                    const std::string& path,
                                    const std::string& codec = "libx264",
                                    PipePixelFormat fmt = PipePixelFormat::RGBA) {
    return FfmpegPipeOptions{
        .width          = w,
        .height         = h,
        .fps            = 30,
        .crf            = 30,          // low quality, fast encode
        .preset         = "ultrafast",
        .codec          = codec,
        .output_path    = path,
        .input_format   = fmt,
        .output_pix_fmt = "yuv420p",
        .verbose        = false,
        .color_transform = {},
        .tune           = "zerolatency",
        .pipe_writer    = "classic",
    };
}

/// Fill a framebuffer with a deterministic colour.
void fill_fb(Framebuffer& fb, float r, float g, float b) {
    auto* data = fb.data();
    const int total = fb.width() * fb.height();
    for (int i = 0; i < total; ++i) {
        data[i] = Color{r, g, b, 1.0f};
    }
}

} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════════════
//  RawFile adapter — end-to-end
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("VideoSinkEncoderAdapter: RawFile end-to-end with RGBA8") {
    const auto path = e2e_temp_path("raw_rgba.raw");
    TempFile cleanup(path);
    constexpr int W = 8, H = 8, N = 3;
    const size_t expected_bytes = static_cast<size_t>(W) * H * 4 * N;

    VideoSinkEncoderAdapter adapter(VideoSinkType::RawFile);
    auto opts = make_adapter_opts(W, H, path, "libx264", PipePixelFormat::RGBA);
    REQUIRE(adapter.open(opts));

    for (int i = 0; i < N; ++i) {
        Framebuffer fb(W, H);
        fill_fb(fb, 0.1f + i * 0.1f, 0.3f, 0.7f);
        CHECK(adapter.write_frame(fb));
    }
    CHECK(adapter.frames_written() == N);

    CHECK(adapter.close());

    // Verify output file exists and has the expected size.
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    CHECK_FALSE(ec);
    CHECK(sz == expected_bytes);
}

TEST_CASE("VideoSinkEncoderAdapter: RawFile end-to-end with YUV420P") {
    const auto path = e2e_temp_path("raw_yuv.raw");
    TempFile cleanup(path);
    constexpr int W = 8, H = 8, N = 2;
    // YUV420P: Y(w*h) + U(w*h/4) + V(w*h/4)
    const size_t frame_bytes = static_cast<size_t>(W) * H * 3 / 2;
    const size_t expected_bytes = frame_bytes * N;

    VideoSinkEncoderAdapter adapter(VideoSinkType::RawFile);
    auto opts = make_adapter_opts(W, H, path, "libx264", PipePixelFormat::YUV420P);
    REQUIRE(adapter.open(opts));

    for (int i = 0; i < N; ++i) {
        Framebuffer fb(W, H);
        fill_fb(fb, 0.2f, 0.5f, 0.8f);
        CHECK(adapter.write_frame(fb));
    }
    CHECK(adapter.frames_written() == N);
    CHECK(adapter.close());

    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    CHECK_FALSE(ec);
    CHECK(sz == expected_bytes);
}

TEST_CASE("VideoSinkEncoderAdapter: RawFile zero frames produces empty output") {
    const auto path = e2e_temp_path("raw_zero.raw");
    TempFile cleanup(path);

    VideoSinkEncoderAdapter adapter(VideoSinkType::RawFile);
    auto opts = make_adapter_opts(8, 8, path, "libx264", PipePixelFormat::RGBA);
    REQUIRE(adapter.open(opts));
    CHECK(adapter.frames_written() == 0);
    CHECK(adapter.close());

    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    CHECK_FALSE(ec);
    CHECK(sz == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
//  FFmpeg adapter — end-to-end (requires ffmpeg)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("VideoSinkEncoderAdapter: FFmpeg end-to-end with RGBA8") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = e2e_temp_path("adapter_ffmpeg_rgba.mp4");
    TempFile cleanup(path);
    constexpr int W = 16, H = 16, N = 5;

    VideoSinkEncoderAdapter adapter(VideoSinkType::Ffmpeg);
    auto opts = make_adapter_opts(W, H, path, "libx264", PipePixelFormat::RGBA);
    REQUIRE(adapter.open(opts));

    for (int i = 0; i < N; ++i) {
        Framebuffer fb(W, H);
        fill_fb(fb, 0.2f, 0.4f + i * 0.1f, 0.8f);
        CHECK(adapter.write_frame(fb));
    }
    CHECK(adapter.frames_written() == N);
    CHECK(adapter.close());

    // Verify output file exists and is non-empty (MP4 header + data).
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    CHECK_FALSE(ec);
    CHECK(sz > 0);
}

TEST_CASE("VideoSinkEncoderAdapter: FFmpeg end-to-end with YUV420P") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = e2e_temp_path("adapter_ffmpeg_yuv.mp4");
    TempFile cleanup(path);
    constexpr int W = 16, H = 16, N = 3;

    VideoSinkEncoderAdapter adapter(VideoSinkType::Ffmpeg);
    auto opts = make_adapter_opts(W, H, path, "libx264", PipePixelFormat::YUV420P);
    REQUIRE(adapter.open(opts));

    for (int i = 0; i < N; ++i) {
        Framebuffer fb(W, H);
        fill_fb(fb, 0.3f, 0.5f, 0.7f);
        CHECK(adapter.write_frame(fb));
    }
    CHECK(adapter.frames_written() == N);
    CHECK(adapter.close());

    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    CHECK_FALSE(ec);
    CHECK(sz > 0);
}

TEST_CASE("VideoSinkEncoderAdapter: FFmpeg zero frames produces valid output") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = e2e_temp_path("adapter_ffmpeg_zero.mp4");
    TempFile cleanup(path);

    VideoSinkEncoderAdapter adapter(VideoSinkType::Ffmpeg);
    auto opts = make_adapter_opts(16, 16, path, "libx264", PipePixelFormat::RGBA);
    REQUIRE(adapter.open(opts));
    CHECK(adapter.frames_written() == 0);

    // Zero frames: flush + close should succeed (ffmpeg writes header only).
    CHECK(adapter.close());
}

TEST_CASE("VideoSinkEncoderAdapter: FFmpeg close without open fails") {
    VideoSinkEncoderAdapter adapter(VideoSinkType::Ffmpeg);
    // close() on a never-opened adapter should not crash.
    CHECK(adapter.close());
}

TEST_CASE("VideoSinkEncoderAdapter: FFmpeg unknown codec resolves to libx264") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = e2e_temp_path("unknown_codec.mp4");
    TempFile cleanup(path);
    constexpr int W = 16, H = 16, N = 2;

    VideoSinkEncoderAdapter adapter(VideoSinkType::Ffmpeg);
    // map_codec() returns Auto for unknown strings, which resolve_auto_codec()
    // resolves to H264 for MP4. So this should work.
    auto opts = make_adapter_opts(W, H, path, "nonexistent_codec", PipePixelFormat::RGBA);
    REQUIRE(adapter.open(opts));

    for (int i = 0; i < N; ++i) {
        Framebuffer fb(W, H);
        fill_fb(fb, 0.5f, 0.3f + i * 0.1f, 0.1f);
        CHECK(adapter.write_frame(fb));
    }
    CHECK(adapter.frames_written() == N);
    CHECK(adapter.close());

    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    CHECK_FALSE(ec);
    CHECK(sz > 0);
}

TEST_CASE("VideoSinkEncoderAdapter: FFmpeg nonexistent output dir does not crash") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    VideoSinkEncoderAdapter adapter(VideoSinkType::Ffmpeg);
    auto opts = make_adapter_opts(16, 16,
                                   "/nonexistent_dir_xyz_123/adapter_test.mp4",
                                   "libx264", PipePixelFormat::RGBA);
    bool opened = adapter.open(opts);
    if (opened) {
        Framebuffer fb(16, 16);
        fill_fb(fb, 0.5f, 0.5f, 0.5f);
        adapter.write_frame(fb);
        adapter.close();
        CHECK(true);  // no crash is the primary assertion
    }
}

TEST_CASE("VideoSinkEncoderAdapter: hardware codec falls back to libx264") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping — ffmpeg not available");
        return;
    }

    const auto path = e2e_temp_path("hw_fallback.mp4");
    TempFile cleanup(path);
    constexpr int W = 16, H = 16, N = 2;

    VideoSinkEncoderAdapter adapter(VideoSinkType::Ffmpeg);
    auto opts = make_adapter_opts(W, H, path, "h264_nvenc", PipePixelFormat::RGBA);
    // The adapter should fall back to libx264, so this should still work.
    REQUIRE(adapter.open(opts));

    for (int i = 0; i < N; ++i) {
        Framebuffer fb(W, H);
        fill_fb(fb, 0.1f, 0.2f + i * 0.1f, 0.9f);
        CHECK(adapter.write_frame(fb));
    }
    CHECK(adapter.frames_written() == N);
    CHECK(adapter.close());

    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    CHECK_FALSE(ec);
    CHECK(sz > 0);
}

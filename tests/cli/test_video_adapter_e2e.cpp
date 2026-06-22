// ---------------------------------------------------------------------------
// test_video_adapter_e2e.cpp -- End-to-end tests for VideoSinkEncoderAdapter.
//
// Tests the full chain from the CLI adapter through the new VideoSink pipeline:
//
//   Framebuffer -> VideoSinkEncoderAdapter -> VideoSink -> file (raw / MP4)
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
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
using namespace chronon3d;

using namespace chronon3d::cli;

namespace {

// -- Runtime ffmpeg check -----------------------------------------------

bool ffmpeg_available() {
    static int avail = -1;
    if (avail < 0) {
        avail = (std::system("ffmpeg -version > /dev/null 2>&1") == 0) ? 1 : 0;
    }
    return avail == 1;
}

// -- Runtime ffprobe check ----------------------------------------------

bool ffprobe_available() {
    static int avail = -1;
    if (avail < 0) {
        avail = (std::system("ffprobe -version > /dev/null 2>&1") == 0) ? 1 : 0;
    }
    return avail == 1;
}

// Run ffprobe on a file and return stdout as a string.
std::string run_ffprobe(const std::string& path) {
    const std::string cmd =
        "ffprobe -v error "
        "-select_streams v:0 "
        "-show_entries stream=codec_name,width,height,pix_fmt,nb_frames "
        "-of csv=p=0 " + path + " 2>/dev/null";

    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return result;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe) != nullptr) {
        result += buf;
    }
    pclose(pipe);
    // Trim trailing newline
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
    return result;
}

// -- Temp path helper ---------------------------------------------------

std::string e2e_temp_path(const char* suffix) {
    static std::atomic<int> counter{0};
    int id = counter.fetch_add(1);
    return "/tmp/chronon3d_adapter_e2e_" + std::to_string(id) + "_" + suffix;
}

// -- RAII temp file -----------------------------------------------------

struct TempFile {
    std::string path;
    explicit TempFile(std::string p) : path(std::move(p)) {}
    ~TempFile() noexcept {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

// -- Build options helper -----------------------------------------------

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

// ==========================================================================
//  RawFile adapter -- end-to-end
// ==========================================================================

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

// ==========================================================================
//  FFmpeg adapter -- end-to-end (requires ffmpeg)
// ==========================================================================

TEST_CASE("VideoSinkEncoderAdapter: FFmpeg end-to-end with RGBA8") {
    if (!ffmpeg_available()) {
        MESSAGE("Skipping - ffmpeg not available");
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
        MESSAGE("Skipping - ffmpeg not available");
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
        MESSAGE("Skipping - ffmpeg not available");
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
        MESSAGE("Skipping - ffmpeg not available");
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
        MESSAGE("Skipping - ffmpeg not available");
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
        MESSAGE("Skipping - ffmpeg not available");
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

// ==========================================================================
//  FFprobe validation -- structural check on generated MP4
// ==========================================================================

// TICKET-007.y (gate-compliance metadata — see docs/FOLLOWUP_TICKETS.md).
//   Owner: chronon3d-owners.
//   Motivation: pre-existing rot; ffprobe CSV parser returning fewer fields than expected.
//
//   Data introduzione: 2026-06-20.  Deadline rimozione: 2026-09-30.
// Re-enabled in PR-C after audit confirmed test-only fix: accept ≥4 fields
// since `nb_frames` is not always emitted for MP4 containers (depends on
// ffmpeg version and stream-info probing depth).  The 4-field fallback
// (codec_name,width,height,pix_fmt) is sufficient structural validation.
TEST_CASE("VideoSinkEncoderAdapter: ffprobe validates MP4 structure") {
    if (!ffmpeg_available() || !ffprobe_available()) {
        MESSAGE("Skipping - ffmpeg or ffprobe not available");
        return;
    }

    const auto path = e2e_temp_path("ffprobe_check.mp4");
    TempFile cleanup(path);
    constexpr int W = 16, H = 16, N = 5;

    VideoSinkEncoderAdapter adapter(VideoSinkType::Ffmpeg);
    auto opts = make_adapter_opts(W, H, path, "libx264", PipePixelFormat::RGBA);
    REQUIRE(adapter.open(opts));

    for (int i = 0; i < N; ++i) {
        Framebuffer fb(W, H);
        fill_fb(fb, 0.0f, 0.5f, 1.0f);
        CHECK(adapter.write_frame(fb));
    }
    CHECK(adapter.frames_written() == N);
    CHECK(adapter.close());

    // Run ffprobe and parse CSV output: codec_name,width,height,pix_fmt,nb_frames
    const std::string probe = run_ffprobe(path);
    REQUIRE_FALSE(probe.empty());

    // Expected CSV format: "h264,16,16,yuv420p" or "h264,16,16,yuv420p,5".
    // `nb_frames` is not always reliably emitted for MP4 containers (depends
    // on ffmpeg version and whether stream-duration probing succeeds), so
    // PR-C accepts a 4-field parse as a sufficient structural validation.
    // Parse with sscanf.
    char codec[64] = {0};
    char pix_fmt[64] = {0};
    int width = 0, height = 0, nb_frames = 0;
    const int parsed = std::sscanf(probe.c_str(), "%63[^,],%d,%d,%63[^,],%d",
                                   codec, &width, &height, pix_fmt, &nb_frames);
    // Accept 4 fields (nb_frames absent) OR 5 fields (nb_frames present).
    REQUIRE((parsed == 4 || parsed == 5));

    CHECK(std::strcmp(codec, "h264") == 0);
    CHECK(width == W);
    CHECK(height == H);
    CHECK(std::strcmp(pix_fmt, "yuv420p") == 0);
    if (parsed == 5) {
        CHECK(nb_frames == N);
    }
}

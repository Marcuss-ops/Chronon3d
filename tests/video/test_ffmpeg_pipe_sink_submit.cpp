#include <doctest/doctest.h>
#include "test_ffmpeg_pipe_sink_helpers.hpp"
#include "src/media/video/ffmpeg_pipe_sink.hpp"
#include <filesystem>
#include <fstream>
#include <vector>
using namespace chronon3d;
using namespace chronon3d::media::video;
using namespace chronon3d::media::video::test_ffpipe;

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

    CHECK(sink.flush());
    CHECK(sink.state() == VideoSinkState::Open);

    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);

    CHECK(sink.frames_submitted() == N);

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
    CHECK(sink.close());
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

    CHECK(sink.flush());
    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);
    CHECK(sink.frames_submitted() == 0);
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
    constexpr int STRIDE = W * 4 + 8;
    const size_t tight_frame = static_cast<size_t>(W) * H * 4;
    const size_t padded_size = static_cast<size_t>(STRIDE) * H;

    FfmpegPipeSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

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

    const auto bare_path = temp_path("bare");
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

    CHECK(std::filesystem::exists(expected_path));
}

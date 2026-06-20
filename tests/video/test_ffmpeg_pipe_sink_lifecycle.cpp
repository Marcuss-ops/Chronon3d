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
    CHECK(sink.state() == VideoSinkState::Created);
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

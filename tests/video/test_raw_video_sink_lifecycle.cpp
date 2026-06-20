#include <doctest/doctest.h>
#include "test_raw_video_sink_helpers.hpp"
#include "src/media/video/raw_video_sink.hpp"
#include <filesystem>
#include <fstream>
#include <vector>
using namespace chronon3d;
using namespace chronon3d::media::video;
using namespace chronon3d::media::video::test_raw_sink;

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

TEST_CASE("RawVideoSink: open from Closed state fails (Closed is terminal)") {
    const auto path = temp_path("reopen");

    RawVideoSink sink;
    {
        auto cfg = make_config(8, 8, PixelFormat::RGBA8, path);
        REQUIRE(sink.open(cfg));
        sink.close();
        CHECK(sink.state() == VideoSinkState::Closed);
    }

    const auto path2 = temp_path("reopen2");
    auto cfg2 = make_config(16, 16, PixelFormat::RGBA8, path2);
    CHECK_FALSE(sink.open(cfg2));
    CHECK(sink.state() == VideoSinkState::Failed);

    std::filesystem::remove(path);
    std::filesystem::remove(path2);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Edge cases: open() while already Open is rejected
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("RawVideoSink: open while already open transitions to Failed") {
    const auto path = temp_path("double_open");

    RawVideoSink sink;
    auto cfg = make_config(8, 8, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Open);

    CHECK_FALSE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: open stays in Open after flush (not Flushing)") {
    const auto path = temp_path("open_after_flush");

    RawVideoSink sink;
    auto cfg = make_config(8, 8, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Open);

    sink.flush();
    CHECK(sink.state() == VideoSinkState::Open);

    std::vector<uint8_t> buf(8 * 8 * 4, 0x42);
    VideoFrameView fv;
    fv.data          = buf.data();
    fv.width         = 8;
    fv.height        = 8;
    fv.pixel_format  = PixelFormat::RGBA8;
    fv.stride_bytes  = 0;
    CHECK(sink.submit(fv));

    sink.close();
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
    CHECK(sink.state() == VideoSinkState::Open);

    std::vector<uint8_t> buf2(8 * 8 * 4, 0x42);
    VideoFrameView fv2;
    fv2.data          = buf2.data();
    fv2.width         = 8;
    fv2.height        = 8;
    fv2.pixel_format  = PixelFormat::RGBA8;
    fv2.stride_bytes  = 0;
    CHECK(sink.submit(fv2));

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
    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);
}

TEST_CASE("RawVideoSink: close on closed sink is idempotent") {
    const auto path = temp_path("close_idempotent");
    RawVideoSink sink;
    auto cfg = make_config(8, 8, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));
    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);

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
//  Stats tracking
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("RawVideoSink: stats accumulate across multiple submits") {
    const auto path = temp_path("stats_accum");
    constexpr int W = 4, H = 4, N = 3;
    const size_t frame_bytes = static_cast<size_t>(W) * H * 4;

    RawVideoSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

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

    RawVideoSink sink;
    CHECK(sink.state() == VideoSinkState::Created);

    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    CHECK(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Open);

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

    CHECK(sink.flush());
    CHECK(sink.state() == VideoSinkState::Open);

    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);

    CHECK(file_has_size(path, N * frame_bytes));

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: stats persist after close") {
    const auto path = temp_path("stats_persist");

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
    CHECK(sink.frames_submitted() == 1);

    sink.close();
    CHECK(sink.state() == VideoSinkState::Closed);
    CHECK(sink.frames_submitted() == 1);
    CHECK(sink.stats().frames_submitted == 1);

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: destructor closes open sink without throwing") {
    const auto path = temp_path("dtor_close");
    {
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
    }
    CHECK(std::filesystem::exists(path));
    CHECK(std::filesystem::file_size(path) == 8 * 8 * 4);

    std::filesystem::remove(path);
}

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
//  Session contract validation
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("RawVideoSink: submit with mismatched pixel format fails") {
    const auto path = temp_path("mismatch_fmt");
    constexpr int W = 8, H = 8;

    RawVideoSink sink;
    auto cfg = make_config(W, H, PixelFormat::YUV420P, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(static_cast<size_t>(W) * H * 4);
    VideoFrameView fv;
    fv.data          = buf.data();
    fv.width         = W;
    fv.height        = H;
    fv.pixel_format  = PixelFormat::RGBA8;
    fv.stride_bytes  = 0;

    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.state() == VideoSinkState::Failed);

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: submit with mismatched dimensions fails") {
    const auto path = temp_path("mismatch_dims");

    RawVideoSink sink;
    auto cfg = make_config(8, 8, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(16 * 16 * 4);
    VideoFrameView fv;
    fv.data          = buf.data();
    fv.width         = 16;
    fv.height        = 16;
    fv.pixel_format  = PixelFormat::RGBA8;
    fv.stride_bytes  = 0;

    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.state() == VideoSinkState::Failed);

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: open with YUV odd width fails") {
    const auto path = temp_path("yuv_odd_w");

    RawVideoSink sink;
    auto cfg = make_config(9, 8, PixelFormat::YUV420P, path);
    CHECK_FALSE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Failed);

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: open with YUV odd height fails") {
    const auto path = temp_path("yuv_odd_h");

    RawVideoSink sink;
    auto cfg = make_config(8, 9, PixelFormat::NV12, path);
    CHECK_FALSE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Failed);

    std::filesystem::remove(path);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Null plane / null data validation
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("RawVideoSink: submit with null data fails") {
    const auto path = temp_path("null_data");

    RawVideoSink sink;
    auto cfg = make_config(8, 8, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    VideoFrameView fv;
    fv.data          = nullptr;
    fv.width         = 8;
    fv.height        = 8;
    fv.pixel_format  = PixelFormat::RGBA8;
    fv.stride_bytes  = 0;

    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: submit_planar with null y_data fails") {
    const auto path = temp_path("null_ydata");

    RawVideoSink sink;
    auto cfg = make_config(8, 8, PixelFormat::YUV420P, path);
    REQUIRE(sink.open(cfg));

    PlanarVideoFrameView pfv;
    pfv.y_data   = nullptr;
    pfv.u_data   = reinterpret_cast<const void*>(0x1);
    pfv.v_data   = reinterpret_cast<const void*>(0x1);
    pfv.width    = 8;
    pfv.height   = 8;
    pfv.y_stride = 0;
    pfv.u_stride = 0;
    pfv.v_stride = 0;

    CHECK_FALSE(sink.submit_planar(pfv));
    CHECK(sink.state() == VideoSinkState::Failed);

    sink.close();
    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: submit with null data pointer transitions to Failed") {
    const auto path = temp_path("null_data_fail");

    RawVideoSink sink;
    auto cfg = make_config(8, 8, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Open);

    VideoFrameView fv;
    fv.data          = nullptr;
    fv.width         = 8;
    fv.height        = 8;
    fv.pixel_format  = PixelFormat::RGBA8;
    fv.stride_bytes  = 0;

    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.state() == VideoSinkState::Failed);
    CHECK(sink.frames_submitted() == 0);

    CHECK(std::filesystem::exists(path));
    CHECK(std::filesystem::file_size(path) == 0);

    sink.close();
    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: submit with zero-size frame (width=0) transitions to Failed") {
    const auto path = temp_path("zero_size");

    RawVideoSink sink;
    auto cfg = make_config(8, 8, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(4);

    VideoFrameView fv;
    fv.data          = buf.data();
    fv.width         = 0;
    fv.height        = 8;
    fv.pixel_format  = PixelFormat::RGBA8;
    fv.stride_bytes  = 0;

    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.state() == VideoSinkState::Failed);
    CHECK(sink.frames_submitted() == 0);

    sink.close();
    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: submit with zero-size frame (height=0) transitions to Failed") {
    const auto path = temp_path("zero_size_h");

    RawVideoSink sink;
    auto cfg = make_config(8, 8, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> buf(4);

    VideoFrameView fv;
    fv.data          = buf.data();
    fv.width         = 8;
    fv.height        = 0;
    fv.pixel_format  = PixelFormat::RGBA8;
    fv.stride_bytes  = 0;

    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.state() == VideoSinkState::Failed);
    CHECK(sink.frames_submitted() == 0);

    sink.close();
    std::filesystem::remove(path);
}

// ═════════════════════════════════════════════════════════════════════════════
//  Edge cases: Failed state — no operations allowed
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("RawVideoSink: Failed state rejects open, submit, and flush") {
    const auto path = temp_path("failed_recovery");

    RawVideoSink sink;
    auto cfg = make_config(8, 8, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    VideoFrameView fv;
    fv.data          = nullptr;
    fv.width         = 8;
    fv.height        = 8;
    fv.pixel_format  = PixelFormat::RGBA8;
    fv.stride_bytes  = 0;
    sink.submit(fv);
    REQUIRE(sink.state() == VideoSinkState::Failed);

    std::vector<uint8_t> buf(8 * 8 * 4);
    fv.data = buf.data();
    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.state() == VideoSinkState::Failed);

    PlanarVideoFrameView pfv;
    pfv.y_data  = buf.data();
    pfv.u_data  = buf.data();
    pfv.v_data  = buf.data();
    pfv.width   = 8;
    pfv.height  = 8;
    CHECK_FALSE(sink.submit_planar(pfv));
    CHECK(sink.state() == VideoSinkState::Failed);

    BiplanarVideoFrameView bfv;
    bfv.y_data   = buf.data();
    bfv.uv_data  = buf.data();
    bfv.width    = 8;
    bfv.height   = 8;
    CHECK_FALSE(sink.submit_biplanar(bfv));
    CHECK(sink.state() == VideoSinkState::Failed);

    CHECK_FALSE(sink.flush());
    CHECK(sink.state() == VideoSinkState::Failed);

    const auto path2 = temp_path("failed_reopen");
    auto cfg2 = make_config(8, 8, PixelFormat::RGBA8, path2);
    CHECK_FALSE(sink.open(cfg2));
    CHECK(sink.state() == VideoSinkState::Failed);

    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: Failed state from invalid directory persists across operations") {
    RawVideoSink sink;
    auto cfg = make_config(16, 16, PixelFormat::RGBA8,
                           "/nonexistent_dir/fail_test.raw");
    CHECK_FALSE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Failed);

    std::vector<uint8_t> buf(16 * 16 * 4);
    VideoFrameView fv;
    fv.data          = buf.data();
    fv.width         = 16;
    fv.height        = 16;
    fv.pixel_format  = PixelFormat::RGBA8;

    CHECK_FALSE(sink.submit(fv));
    CHECK_FALSE(sink.flush());

    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);
}

TEST_CASE("RawVideoSink: close from Failed state cleans up") {
    const auto path = temp_path("failed_close");

    RawVideoSink sink;
    auto cfg = make_config(8, 8, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    VideoFrameView fv;
    fv.data          = nullptr;
    fv.width         = 8;
    fv.height        = 8;
    fv.pixel_format  = PixelFormat::RGBA8;
    sink.submit(fv);
    REQUIRE(sink.state() == VideoSinkState::Failed);

    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);

    std::vector<uint8_t> buf(8 * 8 * 4);
    fv.data = buf.data();
    CHECK_FALSE(sink.submit(fv));

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: open from Failed state is rejected") {
    const auto path = temp_path("failed_open");

    RawVideoSink sink;
    auto cfg = make_config(0, 16, PixelFormat::RGBA8, path);
    CHECK_FALSE(sink.open(cfg));
    CHECK(sink.state() == VideoSinkState::Failed);

    auto cfg2 = make_config(16, 16, PixelFormat::RGBA8, path);
    CHECK_FALSE(sink.open(cfg2));
    CHECK(sink.state() == VideoSinkState::Failed);

    CHECK(sink.close());
    CHECK(sink.state() == VideoSinkState::Closed);

    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: submit after flush succeeds (flush stays Open)") {
    const auto path = temp_path("submit_after_flush");

    RawVideoSink sink;
    auto cfg = make_config(8, 8, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    CHECK(sink.flush());
    CHECK(sink.state() == VideoSinkState::Open);

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
    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: submit with format different from session fails") {
    const auto path = temp_path("format_mismatch");

    RawVideoSink sink;
    auto cfg = make_config(8, 8, PixelFormat::YUV420P, path);
    REQUIRE(sink.open(cfg));

    const size_t rgba_size = 8 * 8 * 4;
    std::vector<uint8_t> buf(rgba_size);
    fill_pattern(buf.data(), rgba_size);

    VideoFrameView fv;
    fv.data          = buf.data();
    fv.width         = 8;
    fv.height        = 8;
    fv.pixel_format  = PixelFormat::RGBA8;
    fv.stride_bytes  = 0;

    CHECK_FALSE(sink.submit(fv));
    CHECK(sink.state() == VideoSinkState::Failed);
    CHECK(sink.frames_submitted() == 0);

    sink.close();
    std::filesystem::remove(path);
}

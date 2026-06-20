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
    constexpr int Y_STRIDE = 16;
    constexpr int UV_STRIDE = 10;
    const size_t y_size  = static_cast<size_t>(W) * H;
    const size_t uv_size = y_size / 4;
    const size_t total   = y_size + uv_size * 2;

    RawVideoSink sink;
    auto cfg = make_config(W, H, PixelFormat::YUV420P, path);
    REQUIRE(sink.open(cfg));

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
        const uint8_t* src = u_padded.data() + y * UV_STRIDE;
        const uint8_t* dst = file_buf.data() + y_size + y * (W / 2);
        CHECK(std::memcmp(dst, src, W / 2) == 0);
    }
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

TEST_CASE("RawVideoSink: submit_planar after flush succeeds") {
    const auto path = temp_path("planar_after_flush");

    RawVideoSink sink;
    auto cfg = make_config(4, 4, PixelFormat::YUV420P, path);
    REQUIRE(sink.open(cfg));

    CHECK(sink.flush());
    CHECK(sink.state() == VideoSinkState::Open);

    std::vector<uint8_t> y_buf(4 * 4);
    std::vector<uint8_t> uv_buf(4);
    PlanarVideoFrameView pfv;
    pfv.y_data   = y_buf.data();
    pfv.u_data   = uv_buf.data();
    pfv.v_data   = uv_buf.data();
    pfv.width    = 4;
    pfv.height   = 4;

    CHECK(sink.submit_planar(pfv));
    CHECK(sink.frames_submitted() == 1);

    sink.close();
    std::filesystem::remove(path);
}

TEST_CASE("RawVideoSink: submit_biplanar after flush succeeds") {
    const auto path = temp_path("biplanar_after_flush");

    RawVideoSink sink;
    auto cfg = make_config(4, 4, PixelFormat::NV12, path);
    REQUIRE(sink.open(cfg));

    CHECK(sink.flush());
    CHECK(sink.state() == VideoSinkState::Open);

    std::vector<uint8_t> y_buf(4 * 4);
    std::vector<uint8_t> uv_buf(4 * 4 / 2);
    BiplanarVideoFrameView bfv;
    bfv.y_data   = y_buf.data();
    bfv.uv_data  = uv_buf.data();
    bfv.width    = 4;
    bfv.height   = 4;

    CHECK(sink.submit_biplanar(bfv));
    CHECK(sink.frames_submitted() == 1);

    sink.close();
    std::filesystem::remove(path);
}

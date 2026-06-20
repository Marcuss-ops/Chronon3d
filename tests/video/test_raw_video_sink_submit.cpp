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
    fv.stride_bytes  = 0;

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
//  submit() — YUV420P, NV12, RGB24
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

    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> actual(frame_bytes);
    file.read(reinterpret_cast<char*>(actual.data()),
              static_cast<std::streamsize>(frame_bytes));
    CHECK(file.gcount() == static_cast<std::streamsize>(frame_bytes));

    CHECK(std::memcmp(expected.data(), actual.data(), frame_bytes) == 0);

    std::filesystem::remove(path);
}

// ═════════════════════════════════════════════════════════════════════════════
//  submit() with non-tight stride (row padding)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("RawVideoSink: submit RGBA8 with row padding (non-zero stride)") {
    const auto path = temp_path("submit_stride");
    constexpr int W = 4, H = 4;
    constexpr int STRIDE = 24;
    const size_t tight_frame = static_cast<size_t>(W) * H * 4;
    const size_t padded_size = static_cast<size_t>(STRIDE) * H;

    RawVideoSink sink;
    auto cfg = make_config(W, H, PixelFormat::RGBA8, path);
    REQUIRE(sink.open(cfg));

    std::vector<uint8_t> padded(padded_size, 0xAA);
    std::vector<uint8_t> expected(tight_frame);
    for (size_t i = 0; i < tight_frame; ++i) {
        expected[i] = static_cast<uint8_t>(i * 13 + 0x77);
    }
    for (int y = 0; y < H; ++y) {
        std::memcpy(padded.data() + y * STRIDE,
                    expected.data() + y * (W * 4),
                    static_cast<size_t>(W * 4));
    }

    VideoFrameView fv;
    fv.data          = padded.data();
    fv.width         = W;
    fv.height        = H;
    fv.pixel_format  = PixelFormat::RGBA8;
    fv.stride_bytes  = static_cast<std::size_t>(STRIDE);

    CHECK(sink.submit(fv));
    CHECK(sink.frames_submitted() == 1);
    sink.close();

    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> actual(tight_frame);
    file.read(reinterpret_cast<char*>(actual.data()),
              static_cast<std::streamsize>(tight_frame));
    CHECK(std::memcmp(expected.data(), actual.data(), tight_frame) == 0);

    std::filesystem::remove(path);
}

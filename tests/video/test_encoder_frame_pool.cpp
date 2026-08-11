#include <doctest/doctest.h>

#include <chronon3d/media/frame_conversion/encoder_frame_pool.hpp>

#include <cstdint>
#include <vector>

using namespace chronon3d::video;

TEST_CASE("EncoderFramePool: preallocates fixed slots and releases RAII borrows") {
    EncoderFramePool pool({
        .width = 16,
        .height = 8,
        .format = EncoderPixelFormat::YUV420P,
        .slot_count = 2,
    });

    CHECK(pool.frame_bytes() == 16u * 8u * 3u / 2u);
    CHECK(pool.stats().slots_allocated == 2);
    CHECK(pool.stats().allocated_bytes == pool.frame_bytes() * 2);

    auto first = pool.acquire();
    auto second = pool.acquire();
    REQUIRE(first);
    REQUIRE(second);
    CHECK(pool.stats().active_slots == 2);
    CHECK_FALSE(pool.acquire());

    first.storage[0] = 0x5A;
    const auto first_address = first.storage.data();
    first = {};
    CHECK(pool.stats().active_slots == 1);

    auto recycled = pool.acquire();
    REQUIRE(recycled);
    CHECK(recycled.storage.data() == first_address);
    CHECK(recycled.storage[0] == 0x5A);
    CHECK(pool.stats().slot_reuses == 1);
}

TEST_CASE("EncoderFramePool: exposes tight YUV420P and NV12 plane layouts") {
    EncoderFramePool yuv_pool({
        .width = 8, .height = 4,
        .format = EncoderPixelFormat::YUV420P,
        .slot_count = 1,
    });
    auto yuv = yuv_pool.acquire();
    REQUIRE(yuv);
    CHECK(yuv.planes.y == yuv.storage.data());
    CHECK(yuv.planes.u == yuv.storage.data() + 32);
    CHECK(yuv.planes.v == yuv.storage.data() + 40);
    CHECK(yuv.planes.stride_y == 8);
    CHECK(yuv.planes.stride_u == 4);
    CHECK(yuv.planes.stride_v == 4);

    EncoderFramePool nv12_pool({
        .width = 8, .height = 4,
        .format = EncoderPixelFormat::NV12,
        .slot_count = 1,
    });
    auto nv12 = nv12_pool.acquire();
    REQUIRE(nv12);
    CHECK(nv12.planes.y == nv12.storage.data());
    CHECK(nv12.planes.uv == nv12.storage.data() + 32);
    CHECK(nv12.planes.stride_y == 8);
    CHECK(nv12.planes.stride_uv == 8);
}

TEST_CASE("EncoderFramePool: invalid or odd YUV configuration is empty") {
    CHECK(EncoderFramePool::encoded_size(0, 8, EncoderPixelFormat::YUV420P) == 0);
    CHECK(EncoderFramePool::encoded_size(
        chronon3d::media::video::kMaxFrameDimension + 1, 2,
        EncoderPixelFormat::RGBA8) == 0);
    CHECK(EncoderFramePool::encoded_size(20000, 20000,
                                         EncoderPixelFormat::RGBA8) == 0);
    CHECK(EncoderFramePool::encoded_size(7, 8, EncoderPixelFormat::NV12) == 0);
    CHECK(EncoderFramePool::encoded_size(8, 7, EncoderPixelFormat::YUV420P) == 0);

    EncoderFramePool pool({
        .width = 7, .height = 8,
        .format = EncoderPixelFormat::YUV420P,
        .slot_count = 2,
    });
    CHECK(pool.frame_bytes() == 0);
    CHECK_FALSE(pool.acquire());
}

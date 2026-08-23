#include <doctest/doctest.h>

#include <chronon3d/runtime/media_session_pool.hpp>

using namespace chronon3d::runtime;

TEST_CASE("MediaSessionPool reuses idle device-local sessions") {
    MediaSessionPool pool;
    const MediaSessionKey key{
        .codec = MediaCodecId::H264,
        .width = 1920,
        .height = 1080,
        .pixel_format = MediaPixelFormat::NV12,
        .device_id = 0,
        .is_encoder = true,
    };
    pool.register_session(key, ReusableMediaSession{
        .codec_ctx_handle = 11,
        .hw_frames_ctx_handle = 22,
    });

    auto first = pool.acquire(key, 10);
    REQUIRE(first.has_value());
    CHECK(first->in_use);
    CHECK(first->last_used_timestamp == 10);
    CHECK_FALSE(pool.acquire(key, 11).has_value());

    pool.release(key, first->codec_ctx_handle, 12);
    auto reused = pool.acquire(key, 13);
    REQUIRE(reused.has_value());
    CHECK(reused->codec_ctx_handle == 11);
    CHECK(reused->hw_frames_ctx_handle == 22);
    CHECK(reused->last_used_timestamp == 13);
}

#include <doctest/doctest.h>

#include <chronon3d/cache/node_cache_identity_builder.hpp>

using namespace chronon3d;
using namespace chronon3d::cache;

TEST_CASE("NodeCacheIdentityBuilder: reproduces a complete legacy key") {
    const TemporalSampleKey temporal{Frame{10}, 100, 2};
    const auto built = NodeCacheIdentityBuilder{"test_scope"}
        .frame(Frame{10})
        .output(640, 480)
        .hashes(0x11111111, 0x22222222, 0x33333333)
        .temporal(temporal)
        .tile(0, 0, 64)
        .build();

    NodeCacheKey legacy;
    legacy.scope = "test_scope";
    legacy.frame = Frame{10};
    legacy.width = 640;
    legacy.height = 480;
    legacy.params_hash = 0x11111111;
    legacy.source_hash = 0x22222222;
    legacy.input_hash = 0x33333333;
    legacy.temporal_key = temporal;
    legacy.tile_x = 0;
    legacy.tile_y = 0;
    legacy.tile_size = 64;

    CHECK(built == legacy);
    CHECK(built.digest() == legacy.digest());
}

TEST_CASE("NodeCacheIdentityBuilder: omitted optional domains preserve legacy defaults") {
    const auto built = NodeCacheIdentityBuilder{"static"}
        .output(1920, 1080)
        .build();

    NodeCacheKey legacy;
    legacy.scope = "static";
    legacy.width = 1920;
    legacy.height = 1080;

    CHECK(built == legacy);
    CHECK(built.temporal_key == TemporalSampleKey{0, 0, 0});
    CHECK(built.tile_x == -1);
    CHECK(built.tile_y == -1);
    CHECK(built.tile_size == 0);
    CHECK(built.tile_hash == 0);
}

TEST_CASE("NodeCacheIdentityBuilder: camera fold matches the canonical helper") {
    Camera2_5D camera;
    camera.zoom = 1200.0f;
    camera.position.z = -900.0f;

    const auto built = NodeCacheIdentityBuilder{"camera"}
        .params(0x1234)
        .camera(camera)
        .build();

    NodeCacheKey legacy;
    legacy.scope = "camera";
    legacy.params_hash = 0x1234;
    fold_camera_into_params_hash(legacy, camera);

    CHECK(built == legacy);
    CHECK(built.digest() == legacy.digest());

    const auto not_folded = NodeCacheIdentityBuilder{"camera"}
        .params(0x1234)
        .camera_if(false, camera)
        .build();
    CHECK(not_folded.params_hash == 0x1234);
}

#include <doctest/doctest.h>

#include <chronon3d/cache.hpp>
#include <memory>

using namespace chronon3d;
using namespace chronon3d::cache;

TEST_CASE("NodeCacheKey digest changes when fields change") {
    NodeCacheKey a{
        .scope = "layer.effect",
        .frame = 10,
        .width = 1920,
        .height = 1080,
        .params_hash = 1,
        .source_hash = 2,
        .input_hash = 3,
    };
    NodeCacheKey b = a;
    b.frame = 11;

    CHECK(a.digest() != b.digest());
}

TEST_CASE("NodeCache stores and retrieves framebuffer values") {
    NodeCache cache;
    NodeCacheKey key{
        .scope = "node.source",
        .frame = 24,
        .width = 320,
        .height = 240,
        .params_hash = 7,
        .source_hash = 8,
        .input_hash = 9,
    };

    auto fb = std::make_shared<Framebuffer>(2, 2);
    cache.store(key, fb);

    REQUIRE(cache.contains(key));
    REQUIRE(cache.find(key) != nullptr);
    CHECK(cache.find(key).get() == fb.get());
    CHECK(cache.erase(key));
    CHECK(cache.find(key) == nullptr);
}

TEST_CASE("FrameCache stores and retrieves framebuffer values") {
    FrameCache cache;
    FrameCacheKey key{
        .composition_id = "intro",
        .frame = 42,
        .width = 1280,
        .height = 720,
        .scene_hash = 100,
        .render_hash = 200,
    };

    auto fb = std::make_shared<Framebuffer>(4, 4);
    cache.store(key, fb);

    REQUIRE(cache.contains(key));
    const auto* found = cache.find(key);
    REQUIRE(found != nullptr);
    CHECK(found->get() == fb.get());
    cache.clear();
    CHECK(cache.size() == 0);
}

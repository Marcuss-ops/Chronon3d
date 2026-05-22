#include <doctest/doctest.h>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <memory>

using namespace chronon3d;

TEST_CASE("NodeCache: replacing existing key does not leave stale LRU entries") {
    cache::NodeCache c(1000);

    auto a = std::make_shared<Framebuffer>(2, 2);
    auto b = std::make_shared<Framebuffer>(2, 2);

    cache::NodeCacheKey key{.scope = "test", .frame = 1};

    c.store(key, a);
    c.store(key, b);

    CHECK(c.size() == 1);
    CHECK(c.contains(key));
    CHECK(c.stats().current_weight == b->size_bytes());
}

TEST_CASE("NodeCache: does not store entries larger than capacity") {
    cache::NodeCache c(10);

    auto fb = std::make_shared<Framebuffer>(2, 2);
    cache::NodeCacheKey key{.scope = "test", .frame = 1};
    c.store(key, fb);

    CHECK_FALSE(c.contains(key));
    CHECK(c.stats().current_weight == 0);
}

TEST_CASE("NodeCache: clear resets usage and all stats") {
    cache::NodeCache c(1000);
    auto fb = std::make_shared<Framebuffer>(2, 2);
    
    cache::NodeCacheKey key1{.scope = "test", .frame = 1};
    cache::NodeCacheKey key2{.scope = "test", .frame = 2};

    c.store(key1, fb);
    (void)c.get(key1); // hit
    (void)c.get(key2); // miss
    
    CHECK(c.stats().hits == 1);
    CHECK(c.stats().misses == 1);
    
    c.clear();
    
    CHECK(c.stats().current_weight == 0);
    CHECK(c.stats().hits == 0);
    CHECK(c.stats().misses == 0);
}

TEST_CASE("Framebuffer: dimensions validation") {
    CHECK_THROWS_AS(Framebuffer(0, 100), std::invalid_argument);
    CHECK_THROWS_AS(Framebuffer(-1, 100), std::invalid_argument);
    CHECK_THROWS_AS(Framebuffer(100, -1), std::invalid_argument);
    
    // Normal allocation should work
    CHECK_NOTHROW(Framebuffer(100, 100));
}

TEST_CASE("Color: safe hex parsing") {
    SUBCASE("try_from_hex") {
        auto c1 = Color::try_from_hex("#ff0000");
        REQUIRE(c1.has_value());
        CHECK(c1->r == 1.0f);
        CHECK(c1->g == 0.0f);
        CHECK(c1->b == 0.0f);
        CHECK(c1->a == 1.0f);

        auto c2 = Color::try_from_hex("#00ff007f"); // semi-transparent green
        REQUIRE(c2.has_value());
        CHECK(c2->r == 0.0f);
        CHECK(c2->g == 1.0f);
        CHECK(c2->b == 0.0f);
        CHECK(c2->a == doctest::Approx(0.5f).epsilon(0.01));

        CHECK_FALSE(Color::try_from_hex("red").has_value());
        CHECK_FALSE(Color::try_from_hex("#ff").has_value());
        CHECK_FALSE(Color::try_from_hex("#GGGGGG").has_value());
    }
}

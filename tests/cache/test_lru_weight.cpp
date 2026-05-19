#include <doctest/doctest.h>

#include <chronon3d/cache/lru_cache.hpp>
#include <string>

using namespace chronon3d::cache;

TEST_CASE("LruCache respects weight capacity") {
    LruCache<int, std::string> cache(10);

    cache.put(1, "aaaaa", 5);
    cache.put(2, "bbbbb", 5);
    CHECK(cache.contains(1));
    CHECK(cache.contains(2));

    // This insertion should trigger eviction (5+5+6 = 16 > 10)
    cache.put(3, "cccccc", 6);

    // At most one of 1 or 2 should still be present (weight ≤ 10)
    CHECK(cache.stats().current_weight <= 10);
    CHECK(cache.stats().evictions > 0);
}

TEST_CASE("LruCache zero-weight items do not trigger eviction") {
    LruCache<int, std::string> cache(10);

    cache.put(1, "hello", 0);
    cache.put(2, "world", 0);
    CHECK(cache.contains(1));
    CHECK(cache.contains(2));
    CHECK(cache.stats().current_weight == 0);
}

TEST_CASE("LruCache single oversized item is stored but evicts others") {
    LruCache<int, std::string> cache(10);

    // Even though item 1 has weight > capacity, it should be stored
    cache.put(1, "oversized", 20);
    CHECK(cache.contains(1));

    // Now inserting another item should evict item 1
    cache.put(2, "small", 1);
    CHECK(cache.stats().evictions > 0);
    CHECK(cache.stats().current_weight <= 10);
}

TEST_CASE("LruCache put with same key replaces weight") {
    LruCache<int, std::string> cache(10);

    cache.put(1, "five", 5);
    cache.put(2, "five", 5);
    CHECK(cache.stats().current_weight == 10);

    // Replace item 1 with a heavier version
    cache.put(1, "ten", 10);
    // Should evict item 2 to make room
    CHECK(cache.stats().current_weight <= 10);
    CHECK(cache.contains(1));
}

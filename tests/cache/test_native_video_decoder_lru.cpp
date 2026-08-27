#include <doctest/doctest.h>
#include <chronon3d/cache/lru_cache.hpp>

#include <memory>

TEST_CASE("Native decoder frame cache evicts least recently used entry") {
    chronon3d::cache::LruCache<
        std::int64_t, std::shared_ptr<int>> cache(
            64, 1, chronon3d::cache::CapacityMode::Count);

    for (std::int64_t frame = 0; frame < 64; ++frame) {
        cache.put(frame, std::make_shared<int>(static_cast<int>(frame)));
    }

    REQUIRE(cache.get(0).has_value());
    cache.put(64, std::make_shared<int>(64));

    CHECK(cache.get(0).has_value());
    CHECK(!cache.get(1).has_value());
    CHECK(cache.get(64).has_value());
    CHECK(cache.stats().current_size == 64);
}

#include <chronon3d/cache/lru_cache.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <doctest/doctest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <iostream>
using namespace chronon3d;

using namespace chronon3d::cache;

TEST_CASE("LruCache - Sharding Contention") {
    const size_t num_shards = 16;
    const size_t capacity = 1000;
    LruCache<int, int> cache(capacity, num_shards);

    const int num_threads = 8;
    const int ops_per_thread = 5000;
    std::vector<std::thread> threads;

    auto start = profiling::now();

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&cache, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                int key = (t * ops_per_thread + i) % 2000;
                cache.put(key, i);
                cache.get(key);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto end = profiling::now();
    auto duration_ms = static_cast<int64_t>(profiling::duration_ms(start, end));

    std::cout << "Sharded Cache (16 shards) duration: " << duration_ms << "ms" << std::endl;
}

TEST_CASE("LruCache - Single Shard Contention") {
    const size_t num_shards = 1;
    const size_t capacity = 1000;
    LruCache<int, int> cache(capacity, num_shards);

    const int num_threads = 8;
    const int ops_per_thread = 5000;
    std::vector<std::thread> threads;

    auto start = profiling::now();

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&cache, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                int key = (t * ops_per_thread + i) % 2000;
                cache.put(key, i);
                cache.get(key);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto end = profiling::now();
    auto duration_ms = static_cast<int64_t>(profiling::duration_ms(start, end));

    std::cout << "Single Shard Cache duration: " << duration_ms << "ms" << std::endl;
}

TEST_CASE("LruCache - Hit/Miss Stats") {
    LruCache<int, int> cache(10, 2);
    
    cache.get(1); // Miss
    cache.put(1, 100);
    cache.get(1); // Hit
    cache.get(2); // Miss
    
    auto stats = cache.stats();
    CHECK(stats.hits == 1);
    CHECK(stats.misses == 2);
}

TEST_CASE("LruCache - Eviction Stats") {
    LruCache<int, int> cache(2, 1); // 1 shard, capacity 2
    
    cache.put(1, 100);
    cache.put(2, 200);
    cache.put(3, 300); // Should evict 1
    
    auto stats = cache.stats();
    CHECK(stats.evictions == 1);
    CHECK(stats.current_size == 2);
    CHECK(!cache.contains(1));
    CHECK(cache.contains(2));
    CHECK(cache.contains(3));
}

#include <doctest/doctest.h>

#include <chronon3d/cache/lru_cache.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace chronon3d::cache;

namespace {

// A loader that sleeps briefly so concurrent callers have a chance to
// race on the same shard.  Returns a pair<value, weight> as required by
// LruCache::compute_if_absent.
struct SlowLoader {
    std::atomic<int>& call_count;
    int value;
    std::chrono::milliseconds delay;

    std::pair<int, std::size_t> operator()() {
        call_count.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::sleep_for(delay);
        return {value, /*weight=*/1};
    }
};

} // namespace

TEST_CASE("LruCache::compute_if_absent: loader called once for concurrent same-key loads") {
    // 1 shard forces the two threads onto the same lock, so we can
    // definitively assert the loader runs exactly once.
    LruCache<int, int> cache(/*capacity_weight=*/100, /*num_shards=*/1);
    std::atomic<int> call_count{0};

    constexpr int kKey = 42;
    constexpr int kValue = 1234;
    constexpr auto kDelay = std::chrono::milliseconds(50);

    std::atomic<int> ready{0};
    std::atomic<bool> go{false};

    std::thread t1([&]() {
        ready.fetch_add(1, std::memory_order_release);
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
        cache.compute_if_absent(kKey, SlowLoader{call_count, kValue, kDelay});
    });
    std::thread t2([&]() {
        ready.fetch_add(1, std::memory_order_release);
        while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
        cache.compute_if_absent(kKey, SlowLoader{call_count, kValue, kDelay});
    });

    // Wait for both threads to be at the barrier
    while (ready.load(std::memory_order_acquire) < 2) std::this_thread::yield();
    go.store(true, std::memory_order_release);

    t1.join();
    t2.join();

    CHECK(call_count.load() == 1);

    auto stats = cache.stats();
    CHECK(stats.misses == 1);
    CHECK(stats.hits == 1); // second caller found the entry inserted by the first
    CHECK(stats.current_size == 1);
    CHECK(cache.contains(kKey));
}

TEST_CASE("LruCache::compute_if_absent: N threads with different keys all succeed in parallel") {
    // 16 shards, one key per thread → all keys hash to potentially
    // different shards and the loaders should run concurrently.
    constexpr int kThreads = 16;
    LruCache<int, int> cache(/*capacity_weight=*/1000, /*num_shards=*/16);
    std::atomic<int> total_calls{0};
    std::atomic<int> concurrent_in_flight{0};
    std::atomic<int> peak_concurrent{0};

    auto loader = [&]() -> std::pair<int, std::size_t> {
        const int now = concurrent_in_flight.fetch_add(1, std::memory_order_acq_rel) + 1;
        int peak = peak_concurrent.load(std::memory_order_relaxed);
        while (now > peak && !peak_concurrent.compare_exchange_weak(
                                  peak, now,
                                  std::memory_order_acq_rel,
                                  std::memory_order_relaxed)) {
            // retry
        }
        // Hold the loader briefly so other threads have time to enter.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        concurrent_in_flight.fetch_sub(1, std::memory_order_acq_rel);
        total_calls.fetch_add(1, std::memory_order_relaxed);
        return {0, /*weight=*/1};
    };

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&, i]() {
            // Use distinct keys to force distinct shards where possible.
            cache.compute_if_absent(i, loader);
        });
    }
    for (auto& t : threads) t.join();

    CHECK(total_calls.load() == kThreads);
    CHECK(peak_concurrent.load() > 1); // at least two loaders ran in parallel
    CHECK(cache.stats().current_size == static_cast<std::size_t>(kThreads));
}

TEST_CASE("LruCache::compute_if_absent: cached value returned on second call without invoking loader") {
    LruCache<int, int> cache(/*capacity_weight=*/100, /*num_shards=*/2);
    std::atomic<int> call_count{0};

    auto first = cache.compute_if_absent(7, SlowLoader{call_count, 999, std::chrono::milliseconds(0)});
    CHECK(first == 999);
    CHECK(call_count.load() == 1);

    // Second call: loader must NOT be invoked.
    auto second = cache.compute_if_absent(7, SlowLoader{call_count, 0, std::chrono::milliseconds(0)});
    CHECK(second == 999);
    CHECK(call_count.load() == 1);

    auto stats = cache.stats();
    CHECK(stats.misses == 1);
    CHECK(stats.hits == 1);
}

TEST_CASE("LruCache::compute_if_absent: loader exception propagates and nothing is cached") {
    LruCache<int, int> cache(/*capacity_weight=*/100, /*num_shards=*/1);
    std::atomic<int> call_count{0};

    auto throwing_loader = [&]() -> std::pair<int, std::size_t> {
        call_count.fetch_add(1, std::memory_order_relaxed);
        throw std::runtime_error("loader failure");
    };

    CHECK_THROWS_AS(cache.compute_if_absent(1, throwing_loader), std::runtime_error);
    CHECK(!cache.contains(1));
    CHECK(cache.stats().current_size == 0);
    CHECK(cache.stats().misses == 1);

    // After the failed load, a subsequent call must retry (loader called again).
    std::atomic<int> recovery_count{0};
    auto ok_loader = [&]() -> std::pair<int, std::size_t> {
        recovery_count.fetch_add(1, std::memory_order_relaxed);
        return {777, 1};
    };
    auto v = cache.compute_if_absent(1, ok_loader);
    CHECK(v == 777);
    CHECK(recovery_count.load() == 1);
    CHECK(cache.contains(1));
}

TEST_CASE("LruCache::compute_if_absent: oversized value is returned but not cached") {
    LruCache<int, int> cache(/*capacity_weight=*/4, /*num_shards=*/1);
    std::atomic<int> call_count{0};

    auto big = cache.compute_if_absent(1, [&]() -> std::pair<int, std::size_t> {
        call_count.fetch_add(1, std::memory_order_relaxed);
        return {42, /*weight=*/100}; // exceeds shard capacity of 4
    });
    CHECK(big == 42);
    CHECK(call_count.load() == 1);
    // Oversized items are returned to the caller but not inserted.
    CHECK(!cache.contains(1));
    CHECK(cache.stats().current_size == 0);
}

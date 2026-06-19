#include <doctest/doctest.h>

#include <chronon3d/cache/lru_cache.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace chronon3d;

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

TEST_CASE("LruCache::compute_if_absent: slow loader does not block other keys in same shard") {
    // Single shard — if the loader held the mutex, the second key would
    // be blocked.  With in-flight futures, the slow loader runs outside
    // the lock and the second key can proceed immediately.
    LruCache<int, int> cache(/*capacity_weight=*/100, /*num_shards=*/1);

    std::atomic<bool> slow_loader_started{false};
    std::atomic<bool> slow_loader_done{false};
    std::atomic<int> fast_loader_calls{0};

    // Launch a slow load for key 1 on a background thread.
    std::thread slow_thread([&]() {
        cache.compute_if_absent(1, [&]() -> std::pair<int, std::size_t> {
            slow_loader_started.store(true, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            slow_loader_done.store(true, std::memory_order_release);
            return {111, 1};
        });
    });

    // Wait until the slow loader has started (and released the mutex).
    while (!slow_loader_started.load(std::memory_order_acquire))
        std::this_thread::yield();

    // Now load a DIFFERENT key (2) that hashes to the SAME shard.
    // With the old design (loader under mutex), this would block for ~200ms.
    // With in-flight futures, the slow loader released the lock and this
    // should complete almost instantly.
    auto t0 = std::chrono::steady_clock::now();
    auto v = cache.compute_if_absent(2, [&]() -> std::pair<int, std::size_t> {
        fast_loader_calls.fetch_add(1, std::memory_order_relaxed);
        return {222, 1};
    });
    auto t1 = std::chrono::steady_clock::now();

    CHECK(v == 222);
    CHECK(fast_loader_calls.load() == 1);

    // The fast load must have completed well before the slow loader finished.
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    CHECK(elapsed < 100);

    slow_thread.join();
    CHECK(slow_loader_done.load());
    CHECK(cache.contains(1));
    CHECK(cache.contains(2));
}

TEST_CASE("LruCache::compute_if_absent: loader exception propagates to all waiters") {
    // Single shard, two threads.  One loads and throws; the other waits
    // on the future and must also receive the exception.
    LruCache<int, int> cache(/*capacity_weight=*/100, /*num_shards=*/1);

    std::atomic<bool> loader_started{false};
    std::atomic<int> exceptions_seen{0};

    std::thread t1([&]() {
        try {
            cache.compute_if_absent(99, [&]() -> std::pair<int, std::size_t> {
                loader_started.store(true, std::memory_order_release);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                throw std::runtime_error("boom");
            });
        } catch (const std::runtime_error&) {
            exceptions_seen.fetch_add(1, std::memory_order_relaxed);
        }
    });

    std::thread t2([&]() {
        // Wait until t1 has started loading.
        while (!loader_started.load(std::memory_order_acquire))
            std::this_thread::yield();
        try {
            cache.compute_if_absent(99, [&]() -> std::pair<int, std::size_t> {
                return {0, 1}; // should never be called
            });
        } catch (const std::runtime_error&) {
            exceptions_seen.fetch_add(1, std::memory_order_relaxed);
        }
    });

    t1.join();
    t2.join();

    CHECK(exceptions_seen.load() == 2);
    CHECK(!cache.contains(99));
    CHECK(cache.stats().current_size == 0);
}

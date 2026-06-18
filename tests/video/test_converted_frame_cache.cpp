#include <doctest/doctest.h>
#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/media/frame_conversion/converted_frame_cache.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

using namespace chronon3d::video;

// ---------------------------------------------------------------------------
//  LruCache-backed semantics (Commit 3)
// ---------------------------------------------------------------------------

TEST_CASE("ConvertedFrameCache: defaulted ctor uses policy default (128 MiB)") {
    ConvertedFrameCache cache;  // 0 → resolve_cache_policy(ConvertedFrames) → 128 MiB
    CHECK(cache.size() == 0);
    CHECK(cache.hits() == 0);
    CHECK(cache.misses() == 0);
}

TEST_CASE("ConvertedFrameCache: explicit cap=5 with num_shards=1 keeps total=5") {
    // num_shards=1 means capacity_per_shard = cap/1 = 5 (no rounding).
    ConvertedFrameCache cache(5, /*num_shards=*/1);

    const std::vector<uint8_t> payload = {0xCA, 0xFE, 0xBA, 0xBE};
    for (uint64_t d = 1; d <= 5; ++d) {
        ConvertedFrameCacheKey k{.framebuffer_digest = d, .width = 4, .height = 4,
                                 .format = EncoderPixelFormat::YUV420P,
                                 .color_matrix = 0, .apply_gamma = true};
        cache.insert(k, payload.data(), payload.size());
    }
    CHECK(cache.size() == 5);
    CHECK(cache.misses() == 0);  // inserts bypass the miss counter; only lookup() bumps it.

    ConvertedFrameCacheKey k_insert_6{
        .framebuffer_digest = 6, .width = 4, .height = 4,
        .format = EncoderPixelFormat::YUV420P,
        .color_matrix = 0, .apply_gamma = true,
    };
    cache.insert(k_insert_6, payload.data(), payload.size());
    CHECK(cache.size() == 5);                    // LRU evicted, remained at cap
    CHECK(cache.stats().evictions == 1);         // and we observed exactly 1
}

TEST_CASE("ConvertedFrameCache: LRU promotion on hit (weight-mode)") {
    ConvertedFrameCache cache(3, /*num_shards=*/1);

    const std::vector<uint8_t> payload{0x01};
    for (uint64_t d = 1; d <= 3; ++d) {
        ConvertedFrameCacheKey k{.framebuffer_digest = d, .width = 4, .height = 4,
                                 .format = EncoderPixelFormat::YUV420P,
                                 .color_matrix = 0, .apply_gamma = true};
        cache.insert(k, payload.data(), payload.size());
    }
    // Hit on d=1 promotes it. LRU becomes d=2.
    ConvertedFrameCacheKey k_promote{.framebuffer_digest = 1, .width = 4, .height = 4,
                                     .format = EncoderPixelFormat::YUV420P,
                                     .color_matrix = 0, .apply_gamma = true};
    REQUIRE(cache.lookup(k_promote) != nullptr);
    CHECK(cache.hits() == 1);

    // Inserting d=4 should evict d=2 (the actual LRU tail after the promotion).
    ConvertedFrameCacheKey k_new{.framebuffer_digest = 4, .width = 4, .height = 4,
                                 .format = EncoderPixelFormat::YUV420P,
                                 .color_matrix = 0, .apply_gamma = true};
    cache.insert(k_new, payload.data(), payload.size());

    ConvertedFrameCacheKey k2_check{.framebuffer_digest = 2, .width = 4, .height = 4,
                                    .format = EncoderPixelFormat::YUV420P,
                                    .color_matrix = 0, .apply_gamma = true};
    CHECK(cache.lookup(k2_check) == nullptr);
    CHECK(cache.lookup(k_new) != nullptr);          // still resident
    CHECK(cache.lookup(k_promote) != nullptr);      // d=1 also resident (promoted)
}

TEST_CASE("ConvertedFrameCache: stats() reflects hits/misses/evictions") {
    ConvertedFrameCache cache(2, /*num_shards=*/1);

    const std::vector<uint8_t> payload{0xAB};
    ConvertedFrameCacheKey k1{.framebuffer_digest = 10, .width = 4, .height = 4,
                              .format = EncoderPixelFormat::YUV420P,
                              .color_matrix = 0, .apply_gamma = true};
    ConvertedFrameCacheKey k2{.framebuffer_digest = 20, .width = 4, .height = 4,
                              .format = EncoderPixelFormat::YUV420P,
                              .color_matrix = 0, .apply_gamma = true};

    cache.insert(k1, payload.data(), payload.size());
    cache.insert(k2, payload.data(), payload.size());

    // Pre-lookup: no hits, no misses yet (inserts do not touch the miss counter).
    CHECK(cache.hits() == 0);
    CHECK(cache.misses() == 0);
    // 1 hit on k1, 1 hit on k2.
    REQUIRE(cache.lookup(k1) != nullptr);
    REQUIRE(cache.lookup(k2) != nullptr);

    CHECK(cache.hits() == 2);
    CHECK(cache.size() == 2);
    CHECK(cache.stats().current_size == 2);
    CHECK(cache.stats().current_weight == 2);  // Weight mode: 2 entries × 1 byte each = 2
}

TEST_CASE("ConvertedFrameCache: clear() resets hits/misses/size") {
    ConvertedFrameCache cache(4);
    ConvertedFrameCacheKey k{.framebuffer_digest = 99, .width = 2, .height = 2,
                             .format = EncoderPixelFormat::NV12,
                             .color_matrix = 0, .apply_gamma = true};
    const std::vector<uint8_t> v{0x42};
    cache.insert(k, v.data(), v.size());
    REQUIRE(cache.lookup(k) != nullptr);

    cache.clear();

    CHECK(cache.size() == 0);
    CHECK(cache.hits() == 0);
    CHECK(cache.misses() == 0);
    CHECK(cache.lookup(k) == nullptr);
}

TEST_CASE("ConvertedFrameCache: shared_ptr lookup survives eviction") {
    // Returned shared_ptr<const Entry> must keep the entry alive even
    // after a subsequent insert evicts it.  This is the key bullet of
    // the Commit 3 API-change commit message.
    ConvertedFrameCache cache(2, /*num_shards=*/1);

    const std::vector<uint8_t> payload{0xA0, 0xB0, 0xC0};
    ConvertedFrameCacheKey k1{.framebuffer_digest = 1, .width = 4, .height = 4,
                              .format = EncoderPixelFormat::YUV420P,
                              .color_matrix = 0, .apply_gamma = true};
    cache.insert(k1, payload.data(), payload.size());
    (void)payload;

    auto entry = cache.lookup(k1);
    REQUIRE(entry != nullptr);
    REQUIRE(entry->data.size() == 3);
    CHECK(entry->data[0] == 0xA0);
    CHECK(entry->data[2] == 0xC0);

    // Force eviction: insert 3 distinct keys.
    for (uint64_t d : {2ULL, 3ULL, 4ULL}) {
        ConvertedFrameCacheKey k{.framebuffer_digest = d, .width = 4, .height = 4,
                                 .format = EncoderPixelFormat::YUV420P,
                                 .color_matrix = 0, .apply_gamma = true};
        const std::vector<uint8_t> p{0xFF};
        cache.insert(k, p.data(), p.size());
    }
    CHECK(cache.size() == 2);
    CHECK(cache.stats().evictions >= 3);

    // The originally-held shared_ptr must still hold the live bytes.
    CHECK(entry->data.size() == 3);
    CHECK(entry->data[0] == 0xA0);
    CHECK(entry->data[2] == 0xC0);
}

TEST_CASE("ConvertedFrameCache: concurrent lookup on same key does not crash") {
    // Smoke test: spawn N threads that all hit the same key. The
    // sharded-per-shard-mutex impl must serialise cleanly without UB;
    // expected hit count equals N * kHitsPerThread.
    ConvertedFrameCache cache(4);
    const std::vector<uint8_t> payload{0xFD};
    ConvertedFrameCacheKey k{.framebuffer_digest = 777, .width = 4, .height = 4,
                             .format = EncoderPixelFormat::YUV420P,
                             .color_matrix = 0, .apply_gamma = true};
    cache.insert(k, payload.data(), payload.size());

    constexpr int kThreads = 4;
    constexpr int kHitsPerThread = 1000;
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&] {
            for (int i = 0; i < kHitsPerThread; ++i) {
                auto e = cache.lookup(k);
                REQUIRE(e != nullptr);
                CHECK(e->data.size() == 1);
                CHECK(e->data[0] == 0xFD);
            }
        });
    }
    for (auto& th : workers) th.join();

    CHECK(cache.hits() == static_cast<std::size_t>(kThreads * kHitsPerThread));
    CHECK(cache.size() == 1);
}

TEST_CASE("ConvertedFrameCache: concurrent lookup on distinct keys hits multiple shards") {
    // Expose both shards by sprinkling keys across the hash space.
    ConvertedFrameCache cache(8);
    const std::vector<uint8_t> payload{0xAB};
    constexpr int kKeys = 8;
    std::vector<ConvertedFrameCacheKey> keys;
    keys.reserve(kKeys);
    for (int i = 0; i < kKeys; ++i) {
        ConvertedFrameCacheKey k{.framebuffer_digest = static_cast<uint64_t>(i + 1),
                                 .width = 4, .height = 4,
                                 .format = EncoderPixelFormat::YUV420P,
                                 .color_matrix = 0, .apply_gamma = true};
        cache.insert(k, payload.data(), payload.size());
        keys.push_back(k);
    }
    CHECK(cache.size() == static_cast<std::size_t>(kKeys));

    constexpr int kThreads = 4;
    constexpr int kHitsPerThread = 500;
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    // Each thread hammers a different subset of keys so the two shards
    // are exercised in parallel without starving the allocator.
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t] {
            for (int i = 0; i < kHitsPerThread; ++i) {
                const auto& k = keys[(t + i) % kKeys];
                auto e = cache.lookup(k);
                REQUIRE(e != nullptr);
                CHECK(e->data.size() == 1);
                CHECK(e->data[0] == 0xAB);
            }
        });
    }
    for (auto& th : workers) th.join();

    CHECK(cache.hits() == static_cast<std::size_t>(kThreads * kHitsPerThread));
    CHECK(cache.size() == static_cast<std::size_t>(kKeys));
}

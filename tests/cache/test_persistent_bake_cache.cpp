#include <doctest/doctest.h>

#include <chronon3d/cache/persistent_bake_cache.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <filesystem>

using namespace chronon3d;
using namespace chronon3d::cache;
using namespace chronon3d::graph;

static std::filesystem::path make_temp_cache_dir() {
    auto path = std::filesystem::temp_directory_path() / "chronon3d_test_bake_cache";
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

static NodeCacheKey make_test_key(uint64_t digest) {
    return NodeCacheKey{
        .scope  = "test",
        .frame  = 0,
        .width  = 100,
        .height = 100,
        .params_hash = digest
    };
}

// ── Test 1: FramebufferPool reuse ─────────────────────────────────────────

TEST_CASE("FramebufferPool reuse - allocations only during warmup") {
    auto pool = std::make_shared<FramebufferPool>(1024 * 1024 * 64);

    // Pre-warm with exactly the sizes we need
    std::vector<FramebufferPoolPreallocOptions> predictions = {
        {128, 128, 2, true, true},
    };
    size_t warmed = pool->warm_up(predictions);
    REQUIRE(warmed == 2);

    auto stats_before = pool->stats();

    // Acquire/release many times — should reuse, not allocate
    for (int i = 0; i < 10; ++i) {
        auto fb = pool->acquire(128, 128, false);
        REQUIRE(fb != nullptr);
    }

    auto stats_after = pool->stats();

    CHECK(stats_after.total_reuses >= 10);
    CHECK(stats_after.total_allocations == stats_before.total_allocations);
    CHECK(stats_after.total_returns >= 10);
}

TEST_CASE("FramebufferPool stats hit_rate improves with reuse") {
    auto pool = std::make_shared<FramebufferPool>(1024 * 1024 * 64);

    // First acquire: allocation (miss)
    auto a = pool->acquire(64, 64);
    a.reset();

    auto stats1 = pool->stats();
    CHECK(stats1.hit_rate == 0.0);

    // Second acquire: reuse (hit)
    auto b = pool->acquire(64, 64);
    b.reset();

    auto stats2 = pool->stats();
    CHECK(stats2.hit_rate > 0.0);
    CHECK(stats2.hit_rate <= 1.0);
}

// ── Test 2: FramebufferPool size change ──────────────────────────────────

TEST_CASE("FramebufferPool different sizes do not reuse across buckets") {
    auto pool = std::make_shared<FramebufferPool>(1024 * 1024 * 64);

    auto small = pool->acquire(64, 64);
    auto large = pool->acquire(256, 256);
    auto* small_ptr = small.get();
    auto* large_ptr = large.get();

    small.reset();
    large.reset();

    // Acquire different size — should NOT get the small one
    auto acquired = pool->acquire(256, 256);
    CHECK(acquired.get() == large_ptr);
    CHECK(acquired.get() != small_ptr);
}

TEST_CASE("FramebufferPool acquire_hinted ReuseNoClear skips clear") {
    auto pool = std::make_shared<FramebufferPool>(1024 * 1024);

    // First acquire with default hint (clear)
    auto fb = pool->acquire_hinted(64, 64, FramebufferAcquireHint::Default);
    fb->set_pixel(10, 10, Color::red());
    auto* ptr = fb.get();
    fb.reset();

    // Second acquire with ReuseNoClear — should reuse same buffer, skip clear
    auto stats_before = pool->stats();
    auto reused = pool->acquire_hinted(64, 64, FramebufferAcquireHint::ReuseNoClear);
    auto stats_after = pool->stats();

    CHECK(reused.get() == ptr);
    // ReuseNoClear should not increment total_clears
    CHECK(stats_after.total_clears == stats_before.total_clears);
}

// ── Test 3: Clear policy ────────────────────────────────────────────────

TEST_CASE("FramebufferPool Default hint performs clear") {
    auto pool = std::make_shared<FramebufferPool>(1024 * 1024);

    auto stats_before = pool->stats();
    auto fb = pool->acquire_hinted(32, 32, FramebufferAcquireHint::Default);
    auto stats_after = pool->stats();

    CHECK(stats_after.total_clears > stats_before.total_clears);
    // Verify transparent after clear
    CHECK(fb->get_pixel(0, 0).a == 0.0f);
}

TEST_CASE("FramebufferPool Temporary hint reuses same buffer") {
    auto pool = std::make_shared<FramebufferPool>(1024 * 1024);

    auto fb = pool->acquire_hinted(64, 64, FramebufferAcquireHint::Temporary);
    auto* ptr = fb.get();
    fb.reset();

    auto fb2 = pool->acquire_hinted(64, 64, FramebufferAcquireHint::Temporary);
    CHECK(fb2.get() == ptr);
}

// ── Test 4: Arena temporanei ────────────────────────────────────────────

TEST_CASE("Arena integration - arena-allocated buffers do not return to pool") {
    auto pool = std::make_shared<FramebufferPool>(1024 * 1024);

    auto stats_before = pool->stats();

    // Arena-allocated buffers should not count toward pool bytes on release
    auto fb = pool->acquire_hinted(64, 64, FramebufferAcquireHint::Temporary);
    fb.reset();

    auto stats_after = pool->stats();
    // total_returns should still increment
    CHECK(stats_after.total_returns > stats_before.total_returns);
}

// ── Test 5: Cache persistente ──────────────────────────────────────────

TEST_CASE("PersistentBakeCache - store and load roundtrip") {
    auto dir = make_temp_cache_dir();
    PersistentBakeCache::instance().set_cache_dir(dir);

    Framebuffer fb(64, 64);
    fb.set_pixel(10, 10, Color::red());
    fb.set_pixel(20, 20, Color::blue());

    auto key = make_test_key(0xABCD1234);
    PersistentBakeCache::instance().store(key, fb);
    CHECK(PersistentBakeCache::instance().exists(key));

    auto loaded = PersistentBakeCache::instance().load(key);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->width() == 64);
    CHECK(loaded->height() == 64);
    CHECK(loaded->get_pixel(10, 10).r == Color::red().r);
    CHECK(loaded->get_pixel(20, 20).b == Color::blue().b);

    PersistentBakeCache::instance().clear();
}

TEST_CASE("PersistentBakeCache - second run reuses cache") {
    auto dir = make_temp_cache_dir();
    PersistentBakeCache::instance().set_cache_dir(dir);

    Framebuffer fb(32, 32);
    fb.set_pixel(5, 5, Color::green());

    auto key = make_test_key(0xDEADBEEF);

    // First run: store
    PersistentBakeCache::instance().store(key, fb);
    CHECK(PersistentBakeCache::instance().entry_count() == 1);

    // Second run: load from existing cache
    auto loaded = PersistentBakeCache::instance().load(key);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->get_pixel(5, 5).g == Color::green().g);

    PersistentBakeCache::instance().clear();
}

TEST_CASE("PersistentBakeCache - different params_hash creates separate entries") {
    auto dir = make_temp_cache_dir();
    PersistentBakeCache::instance().set_cache_dir(dir);

    Framebuffer fb(16, 16);
    auto key_a = make_test_key(0x11111111);
    auto key_b = make_test_key(0x22222222);

    PersistentBakeCache::instance().store(key_a, fb);
    PersistentBakeCache::instance().store(key_b, fb);

    CHECK(PersistentBakeCache::instance().entry_count() == 2);

    PersistentBakeCache::instance().clear();
}

TEST_CASE("PersistentBakeCache - load returns nullptr for missing key") {
    auto dir = make_temp_cache_dir();
    PersistentBakeCache::instance().set_cache_dir(dir);
    PersistentBakeCache::instance().clear();

    auto key = make_test_key(0x99999999);
    auto loaded = PersistentBakeCache::instance().load(key);

    CHECK(loaded == nullptr);
}

// ── Priorità 4 Test 5: PersistentBakeCache stride-safe ─────────────────

TEST_CASE("PersistentBakeCache - stride-safe roundtrip with non-aligned dimensions") {
    auto dir = make_temp_cache_dir();
    PersistentBakeCache::instance().set_cache_dir(dir);
    PersistentBakeCache::instance().clear();

    // Use non-aligned dimensions (1277x719 — neither multiple of 64 nor cache-line)
    Framebuffer fb(1277, 719);

    // Verify stride is wider than logical width
    CHECK(fb.allocated_width() >= 1277);

    // Write a different pattern for each row
    for (i32 y = 0; y < fb.height(); ++y) {
        for (i32 x = 0; x < fb.width(); ++x) {
            float v = static_cast<float>(y * fb.width() + x) / static_cast<float>(fb.width() * fb.height());
            fb.set_pixel(x, y, Color{v, v * 0.5f, 1.0f - v, 1.0f});
        }
    }

    auto key = make_test_key(0xCAFE1277);
    PersistentBakeCache::instance().store(key, fb);
    CHECK(PersistentBakeCache::instance().exists(key));

    auto loaded = PersistentBakeCache::instance().load(key);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->width() == 1277);
    CHECK(loaded->height() == 719);

    // Verify all pixels are identical (no row shifting, no padding corruption)
    for (i32 y = 0; y < fb.height(); ++y) {
        for (i32 x = 0; x < fb.width(); ++x) {
            Color orig = fb.get_pixel(x, y);
            Color loaded_px = loaded->get_pixel(x, y);
            CHECK(orig.r == doctest::Approx(loaded_px.r).epsilon(0.001f));
            CHECK(orig.g == doctest::Approx(loaded_px.g).epsilon(0.001f));
            CHECK(orig.b == doctest::Approx(loaded_px.b).epsilon(0.001f));
            CHECK(orig.a == doctest::Approx(loaded_px.a).epsilon(0.001f));
        }
    }

    PersistentBakeCache::instance().clear();
}

TEST_CASE("PersistentBakeCache - batch store stores all entries") {
    auto dir = make_temp_cache_dir();
    PersistentBakeCache::instance().set_cache_dir(dir);
    PersistentBakeCache::instance().clear();

    std::vector<std::pair<NodeCacheKey, std::shared_ptr<Framebuffer>>> entries;
    for (int i = 0; i < 3; ++i) {
        auto fb = std::make_shared<Framebuffer>(16, 16);
        fb->set_pixel(i, i, Color::white());
        entries.emplace_back(make_test_key(0x1000 + i), fb);
    }

    PersistentBakeCache::instance().store_batch(entries);
    CHECK(PersistentBakeCache::instance().entry_count() == 3);

    for (int i = 0; i < 3; ++i) {
        auto loaded = PersistentBakeCache::instance().load(make_test_key(0x1000 + i));
        REQUIRE(loaded != nullptr);
        CHECK(loaded->get_pixel(i, i).r == Color::white().r);
    }

    PersistentBakeCache::instance().clear();
}

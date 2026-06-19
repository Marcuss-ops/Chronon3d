// =============================================================================
// test_cache_diagnostics.cpp — CacheDiagnostics registry tests.
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/cache/cache_policy.hpp>

using namespace chronon3d::cache;

namespace {

/// A minimal fake cache that tracks its own hits/clears for test assertions.
struct FakeCache {
    size_t hits{0};
    size_t misses{0};
    size_t evictions{0};
    size_t current_size{0};
    size_t current_weight{0};
    size_t capacity{1024};
    CapacityMode mode{CapacityMode::Weight};
    int clear_count{0};
};

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// §1: Basic registration and snapshot
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("CacheDiagnostics - register and snapshot") {
    auto& diag = CacheDiagnostics::instance();
    diag.set_enabled(true);

    FakeCache fake;
    fake.hits = 10;
    fake.misses = 5;
    fake.evictions = 2;
    fake.current_size = 3;
    fake.current_weight = 512;
    fake.capacity = 1024;
    fake.mode = CapacityMode::Weight;

    auto handle = diag.register_cache(
        CacheDomain::RenderedFrames,
        [&fake]() -> GenericCacheStats {
            return {fake.hits, fake.misses, fake.evictions,
                    fake.current_size, fake.current_weight};
        },
        [&fake] { ++fake.clear_count; },
        [&fake] { return fake.mode; },
        fake.capacity);

    CHECK(handle);
    CHECK(diag.registered_count() == 1);
    CHECK(diag.registered_count(CacheDomain::RenderedFrames) == 1);
    CHECK(diag.registered_count(CacheDomain::Nodes) == 0);

    auto snap = diag.snapshot();
    REQUIRE(snap.size() == 1);
    CHECK(snap[0].domain == CacheDomain::RenderedFrames);
    CHECK(snap[0].hits == 10);
    CHECK(snap[0].misses == 5);
    CHECK(snap[0].evictions == 2);
    CHECK(snap[0].current_size == 3);
    CHECK(snap[0].current_weight == 512);
    CHECK(snap[0].capacity == 1024);
    CHECK(snap[0].mode == CapacityMode::Weight);
}

// ═════════════════════════════════════════════════════════════════════════════
// §2: Multiple domains
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("CacheDiagnostics - multiple domains") {
    auto& diag = CacheDiagnostics::instance();
    diag.set_enabled(true);

    FakeCache fb_cache;
    fb_cache.hits = 5;
    fb_cache.capacity = 512;
    fb_cache.mode = CapacityMode::Weight;

    FakeCache sp_cache;
    sp_cache.hits = 3;
    sp_cache.capacity = 8;
    sp_cache.mode = CapacityMode::Count;

    auto h1 = diag.register_cache(
        CacheDomain::RenderedFrames,
        [&fb_cache]() -> GenericCacheStats {
            return {fb_cache.hits, 0, 0, fb_cache.current_size, fb_cache.current_weight};
        },
        [&fb_cache] { ++fb_cache.clear_count; },
        [&fb_cache] { return fb_cache.mode; },
        fb_cache.capacity);

    auto h2 = diag.register_cache(
        CacheDomain::ScenePrograms,
        [&sp_cache]() -> GenericCacheStats {
            return {sp_cache.hits, 0, 0, sp_cache.current_size, sp_cache.current_weight};
        },
        [&sp_cache] { ++sp_cache.clear_count; },
        [&sp_cache] { return sp_cache.mode; },
        sp_cache.capacity);

    CHECK(diag.registered_count() == 2);

    auto snap = diag.snapshot();
    CHECK(snap.size() == 2);

    // Snapshot by domain
    auto ds_frames = diag.snapshot_by_domain(CacheDomain::RenderedFrames);
    CHECK(ds_frames.domain == CacheDomain::RenderedFrames);
    CHECK(ds_frames.instance_count == 1);
    CHECK(ds_frames.hits == 5);

    auto ds_progs = diag.snapshot_by_domain(CacheDomain::ScenePrograms);
    CHECK(ds_progs.domain == CacheDomain::ScenePrograms);
    CHECK(ds_progs.hits == 3);

    // Non-existent domain
    auto ds_empty = diag.snapshot_by_domain(CacheDomain::Text);
    CHECK(ds_empty.instance_count == 0);
    CHECK(ds_empty.hits == 0);

    // All domains snapshot
    auto all = diag.snapshot_all_domains();
    CHECK(all.size() == 2);
}

// ═════════════════════════════════════════════════════════════════════════════
// §3: Clear by domain
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("CacheDiagnostics - clear by domain") {
    auto& diag = CacheDiagnostics::instance();
    diag.set_enabled(true);

    FakeCache a, b, c;
    auto h1 = diag.register_cache(
        CacheDomain::RenderedFrames,
        [&a]() -> GenericCacheStats { return {}; },
        [&a] { ++a.clear_count; },
        [&a] { return a.mode; }, 1024);
    auto h2 = diag.register_cache(
        CacheDomain::VideoFrames,
        [&b]() -> GenericCacheStats { return {}; },
        [&b] { ++b.clear_count; },
        [&b] { return b.mode; }, 256);
    auto h3 = diag.register_cache(
        CacheDomain::RenderedFrames,
        [&c]() -> GenericCacheStats { return {}; },
        [&c] { ++c.clear_count; },
        [&c] { return c.mode; }, 2048);

    // Clear only RenderedFrames — should hit a and c, not b.
    size_t cleared = diag.clear_by_domain(CacheDomain::RenderedFrames);
    CHECK(cleared == 2);
    CHECK(a.clear_count == 1);
    CHECK(b.clear_count == 0);
    CHECK(c.clear_count == 1);

    // Clear non-existent domain
    cleared = diag.clear_by_domain(CacheDomain::Text);
    CHECK(cleared == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// §4: Clear all
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("CacheDiagnostics - clear all") {
    auto& diag = CacheDiagnostics::instance();
    diag.set_enabled(true);

    FakeCache a, b;
    auto h1 = diag.register_cache(
        CacheDomain::Nodes,
        [&a]() -> GenericCacheStats { return {}; },
        [&a] { ++a.clear_count; },
        [&a] { return a.mode; }, 2048);
    auto h2 = diag.register_cache(
        CacheDomain::ScenePrograms,
        [&b]() -> GenericCacheStats { return {}; },
        [&b] { ++b.clear_count; },
        [&b] { return b.mode; }, 8);

    size_t cleared = diag.clear_all();
    CHECK(cleared == 2);
    CHECK(a.clear_count == 1);
    CHECK(b.clear_count == 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// §5: Unregister on handle destruction
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("CacheDiagnostics - unregister on handle destruction") {
    auto& diag = CacheDiagnostics::instance();
    diag.set_enabled(true);

    FakeCache fake;
    {
        auto handle = diag.register_cache(
            CacheDomain::Nodes,
            [&fake]() -> GenericCacheStats { return {}; },
            [&fake] { ++fake.clear_count; },
            [&fake] { return fake.mode; }, 1024);
        CHECK(diag.registered_count() == 1);
    }
    // Handle destroyed — should be unregistered.
    CHECK(diag.registered_count() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// §6: Handle move semantics
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("CacheDiagnostics - handle move semantics") {
    auto& diag = CacheDiagnostics::instance();
    diag.set_enabled(true);

    FakeCache fake;
    CacheDiagnostics::Handle handle;
    {
        auto tmp = diag.register_cache(
            CacheDomain::Nodes,
            [&fake]() -> GenericCacheStats { return {}; },
            [&fake] { ++fake.clear_count; },
            [&fake] { return fake.mode; }, 1024);
        CHECK(diag.registered_count() == 1);
        handle = std::move(tmp);
        CHECK(!tmp);  // moved-from handle is empty
    }
    CHECK(diag.registered_count() == 1);  // still registered via `handle`
}

// ═════════════════════════════════════════════════════════════════════════════
// §7: Disabled — snapshot/clear are no-ops
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("CacheDiagnostics - disabled mode") {
    auto& diag = CacheDiagnostics::instance();
    diag.set_enabled(false);

    FakeCache fake;
    auto handle = diag.register_cache(
        CacheDomain::Nodes,
        [&fake]() -> GenericCacheStats { return {}; },
        [&fake] { ++fake.clear_count; },
        [&fake] { return fake.mode; }, 1024);

    CHECK(diag.registered_count() == 1);

    // Snapshot returns empty when disabled.
    auto snap = diag.snapshot();
    CHECK(snap.empty());

    // Clear returns 0 when disabled.
    CHECK(diag.clear_all() == 0);
    CHECK(fake.clear_count == 0);

    diag.set_enabled(true);  // restore for other tests
}

// ═════════════════════════════════════════════════════════════════════════════
// §8: DomainSnapshot aggregates correctly
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("CacheDiagnostics - DomainSnapshot aggregation") {
    auto& diag = CacheDiagnostics::instance();
    diag.set_enabled(true);

    FakeCache a, b;
    a.hits = 10; a.misses = 2; a.evictions = 1;
    a.current_size = 3; a.current_weight = 300; a.capacity = 1000;

    b.hits = 5; b.misses = 3; b.evictions = 0;
    b.current_size = 2; b.current_weight = 200; b.capacity = 500;

    auto h1 = diag.register_cache(
        CacheDomain::Nodes,
        [&a]() -> GenericCacheStats {
            return {a.hits, a.misses, a.evictions,
                    a.current_size, a.current_weight};
        },
        [&a] { ++a.clear_count; },
        [&a] { return a.mode; }, a.capacity);
    auto h2 = diag.register_cache(
        CacheDomain::Nodes,
        [&b]() -> GenericCacheStats {
            return {b.hits, b.misses, b.evictions,
                    b.current_size, b.current_weight};
        },
        [&b] { ++b.clear_count; },
        [&b] { return b.mode; }, b.capacity);

    auto ds = diag.snapshot_by_domain(CacheDomain::Nodes);
    CHECK(ds.instance_count == 2);
    CHECK(ds.hits == 15);
    CHECK(ds.misses == 5);
    CHECK(ds.evictions == 1);
    CHECK(ds.current_size == 5);
    CHECK(ds.current_weight == 500);
    CHECK(ds.total_capacity == 1500);
}

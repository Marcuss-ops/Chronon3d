// =============================================================================
// test_cache_policy.cpp — Tests for centralized cache-policy resolver.
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/cache/cache_policy.hpp>

using namespace chronon3d::cache;

TEST_CASE("cache_policy: resolve_cache_policy returns valid policy for every domain") {
    // Every domain must resolve to a non-zero capacity and a valid unit/shard.
    for (auto domain : {
             CacheDomain::Nodes,
             CacheDomain::RenderedFrames,
             CacheDomain::VideoFrames,
             CacheDomain::ConvertedFrames,
             CacheDomain::ScenePrograms,
             CacheDomain::Images,
             CacheDomain::GlyphAtlas,
             CacheDomain::Text,
             CacheDomain::Shadows,
             CacheDomain::Glow,
         }) {
        auto policy = resolve_cache_policy(domain);
        CHECK(policy.capacity > 0);
        CHECK(policy.shards >= 1);
        CHECK(policy.enabled);
        CHECK(policy.domain == domain);
    }
}

TEST_CASE("cache_policy: explicit override takes precedence") {
    // An explicit override must be returned verbatim, ignoring Config/defaults.
    auto policy = resolve_cache_policy(CacheDomain::RenderedFrames,
                                        std::optional<std::size_t>(1234));
    CHECK(policy.capacity == 1234);
    CHECK(policy.domain == CacheDomain::RenderedFrames);
}

TEST_CASE("cache_policy: zero override falls through to default") {
    // Passing 0 means "use default" (matches constructor semantics).
    auto policy = resolve_cache_policy(CacheDomain::ConvertedFrames,
                                        std::optional<std::size_t>(0));
    // Default for ConvertedFrames is 8 entries.
    CHECK(policy.capacity == 8);
    CHECK(policy.unit == CacheCapacityUnit::Entries);
}

TEST_CASE("cache_policy: capacity_mode_for maps Bytes → Weight, Entries → Count") {
    // NodeCache uses Bytes → Weight.
    CHECK(capacity_mode_for(CacheDomain::Nodes) == CapacityMode::Weight);
    // SceneProgramCache uses Entries → Count.
    CHECK(capacity_mode_for(CacheDomain::ScenePrograms) == CapacityMode::Count);
    // FrameCache uses Entries → Count.
    CHECK(capacity_mode_for(CacheDomain::RenderedFrames) == CapacityMode::Count);
}

TEST_CASE("cache_policy: each domain has a consistent unit") {
    // Byte-weighted domains.
    CHECK(resolve_cache_policy(CacheDomain::Nodes).unit == CacheCapacityUnit::Bytes);
    CHECK(resolve_cache_policy(CacheDomain::Images).unit == CacheCapacityUnit::Bytes);
    CHECK(resolve_cache_policy(CacheDomain::Glow).unit == CacheCapacityUnit::Bytes);

    // Count-limited domains.
    CHECK(resolve_cache_policy(CacheDomain::RenderedFrames).unit == CacheCapacityUnit::Entries);
    CHECK(resolve_cache_policy(CacheDomain::VideoFrames).unit == CacheCapacityUnit::Entries);
    CHECK(resolve_cache_policy(CacheDomain::ScenePrograms).unit == CacheCapacityUnit::Entries);
}

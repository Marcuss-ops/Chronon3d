#include <doctest/doctest.h>

#include <chronon3d/render_graph/core/cache_policy.hpp>

using chronon3d::Frame;
using chronon3d::graph::cache_frame_for_policy;
using chronon3d::graph::frame_variant_cache;
using chronon3d::graph::static_memory_cache;

TEST_CASE("cache frame canonicalization collapses frame-invariant nodes") {
    const auto policy = static_memory_cache("test-static");

    CHECK(cache_frame_for_policy(policy, Frame{42}) == Frame{0});
    CHECK(cache_frame_for_policy(policy, Frame{42}, Frame{7}) == Frame{0});
}

TEST_CASE("cache frame canonicalization preserves temporal identity") {
    const auto policy = frame_variant_cache("test-temporal");

    CHECK(cache_frame_for_policy(policy, Frame{42}) == Frame{42});
    CHECK(cache_frame_for_policy(policy, Frame{42}, Frame{7}) == Frame{7});
}

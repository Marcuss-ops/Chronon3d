#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>

using namespace chronon3d;

TEST_CASE("umbrella header exports tracing and counters") {
    RenderTrace trace;
    RenderCounters counters;

    {
        TraceScope scope(&trace, "umbrella", "api", 7);
    }

    REQUIRE(trace.events().size() == 1);
    CHECK(trace.events()[0].name == "umbrella");
    CHECK(trace.events()[0].category == "api");
    CHECK(trace.events()[0].frame == 7);

    counters.pixels_touched.store(12, std::memory_order_relaxed);
    counters.cache_hits.store(3, std::memory_order_relaxed);
    counters.reset();

    CHECK(counters.pixels_touched.load(std::memory_order_relaxed) == 0);
    CHECK(counters.cache_hits.load(std::memory_order_relaxed) == 0);
}

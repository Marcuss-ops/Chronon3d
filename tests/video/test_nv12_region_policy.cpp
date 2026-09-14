#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/media/video/nv12_region_policy.hpp>

#include <vector>

using chronon3d::media::video::Nv12ConversionPlan;
using chronon3d::media::video::plan_nv12_conversion;
using chronon3d::raster::BBox;

namespace {

const std::vector<BBox> kRequested{{100, 200, 300, 400}, {1900, 1000, 1920, 1080}};

BBox whole_frame(std::uint32_t w, std::uint32_t h) {
    return BBox{0, 0, static_cast<int>(w), static_cast<int>(h)};
}

} // namespace

TEST_CASE("nv12 region policy: an empty request is the whole frame") {
    const Nv12ConversionPlan plan =
        plan_nv12_conversion({}, 1920, 1080, /*destination_retains_previous_frame=*/false);
    CHECK(plan.full_frame);
    CHECK_FALSE(plan.partial_rejected);
    REQUIRE(plan.regions.size() == 1);
    CHECK(plan.regions[0] == whole_frame(1920, 1080));
}

TEST_CASE("nv12 region policy: an unproven partial request is widened, not honoured") {
    // This is the contract that makes a torn frame impossible: a producer that
    // passes the CPU renderer's predicted dirty rect (or any other set it
    // cannot back with destination continuity) gets the full frame plus an
    // explicit rejection report.
    const Nv12ConversionPlan plan =
        plan_nv12_conversion(kRequested, 1920, 1080,
                             /*destination_retains_previous_frame=*/false);
    CHECK(plan.full_frame);
    CHECK(plan.partial_rejected);
    REQUIRE(plan.regions.size() == 1);
    CHECK(plan.regions[0] == whole_frame(1920, 1080));
}

TEST_CASE("nv12 region policy: a proven partial request is honoured and normalized") {
    const Nv12ConversionPlan plan =
        plan_nv12_conversion(kRequested, 1920, 1080,
                             /*destination_retains_previous_frame=*/true);
    CHECK_FALSE(plan.full_frame);
    CHECK_FALSE(plan.partial_rejected);
    REQUIRE(plan.regions.size() == 2);
    // 2x2 chroma: origins round down, ends round up.
    CHECK(plan.regions[0] == BBox{100, 200, 300, 400});
    CHECK(plan.regions[1] == BBox{1900, 1000, 1920, 1080});
}

TEST_CASE("nv12 region policy: odd origins and ends land on the chroma grid") {
    const std::vector<BBox> requested{{1, 3, 5, 7}};
    const Nv12ConversionPlan plan =
        plan_nv12_conversion(requested, 64, 64, /*destination_retains_previous_frame=*/true);
    REQUIRE(plan.regions.size() == 1);
    CHECK(plan.regions[0] == BBox{0, 2, 6, 8});
}

TEST_CASE("nv12 region policy: a partial request is clamped to the frame") {
    const std::vector<BBox> requested{{-40, -40, 200, 200}, {1800, 900, 5000, 5000}};
    const Nv12ConversionPlan plan =
        plan_nv12_conversion(requested, 1920, 1080, /*destination_retains_previous_frame=*/true);
    REQUIRE(plan.regions.size() == 2);
    CHECK(plan.regions[0] == BBox{0, 0, 200, 200});
    CHECK(plan.regions[1] == BBox{1800, 900, 1920, 1080});
}

TEST_CASE("nv12 region policy: regions entirely outside the frame drop out") {
    const std::vector<BBox> requested{{-100, -100, -2, -2}, {5000, 5000, 6000, 6000},
                                      {10, 10, 60, 60}};
    const Nv12ConversionPlan plan =
        plan_nv12_conversion(requested, 1920, 1080, /*destination_retains_previous_frame=*/true);
    REQUIRE(plan.regions.size() == 1);
    CHECK(plan.regions[0] == BBox{10, 10, 60, 60});
}

TEST_CASE("nv12 region policy: an all-empty surviving set still converts the frame") {
    const std::vector<BBox> requested{{-100, -100, 0, 0}, {5000, 5000, 6000, 6000}};
    const Nv12ConversionPlan plan =
        plan_nv12_conversion(requested, 1920, 1080, /*destination_retains_previous_frame=*/true);
    CHECK(plan.full_frame);
    REQUIRE(plan.regions.size() == 1);
    CHECK(plan.regions[0] == whole_frame(1920, 1080));
}

TEST_CASE("nv12 region policy: an odd-sized canvas keeps its last column and row") {
    // Alignment must never crop the frame edge: the full-frame request is the
    // conversion that a pooled destination always takes, so an odd canvas has
    // to arrive unchanged.
    const Nv12ConversionPlan plan =
        plan_nv12_conversion({}, 1921, 1081, /*destination_retains_previous_frame=*/false);
    REQUIRE(plan.regions.size() == 1);
    CHECK(plan.regions[0] == BBox{0, 0, 1921, 1081});

    const Nv12ConversionPlan proven =
        plan_nv12_conversion({whole_frame(1921, 1081)}, 1921, 1081,
                             /*destination_retains_previous_frame=*/true);
    REQUIRE(proven.regions.size() == 1);
    CHECK(proven.regions[0] == BBox{0, 0, 1921, 1081});
}

TEST_CASE("nv12 region policy: a degenerate canvas never yields an empty launch set") {
    for (std::uint32_t w : {0u, 1u}) {
        for (std::uint32_t h : {0u, 1u}) {
            for (bool continuity : {false, true}) {
                const Nv12ConversionPlan plan = plan_nv12_conversion(
                    kRequested, w, h, continuity);
                CHECK_FALSE(plan.regions.empty());
            }
        }
    }
}

TEST_CASE("nv12 region policy: every published region stays inside the frame") {
    const std::vector<BBox> requested{{-1, -1, 3, 3}, {1919, 1079, 1919, 1079},
                                      {100, 100, 200, 200}};
    for (bool continuity : {false, true}) {
        const Nv12ConversionPlan plan =
            plan_nv12_conversion(requested, 1920, 1080, continuity);
        REQUIRE_FALSE(plan.regions.empty());
        for (const auto& region : plan.regions) {
            CHECK(region.x0 >= 0);
            CHECK(region.y0 >= 0);
            CHECK(region.x1 <= 1920);
            CHECK(region.y1 <= 1080);
            CHECK_FALSE(region.is_empty());
        }
        // A partial plan reports exactly the regions asked for; a widened plan
        // must cover the frame, never a smaller area than the request.
        if (plan.full_frame) {
            CHECK(plan.regions.size() == 1);
            CHECK(plan.regions[0] == whole_frame(1920, 1080));
        }
    }
}

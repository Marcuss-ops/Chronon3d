#include <doctest/doctest.h>

#include <chronon3d/render_plan/spring_bake.hpp>

#include <algorithm>

TEST_CASE("render plan spring bake is deterministic and preserves bounded overshoot") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    const auto first = bake_spring_progress(Spring::Bouncy, 0.5, 30, 1);
    const auto repeated = bake_spring_progress(Spring::Bouncy, 0.5, 30, 1);
    REQUIRE(first.size() == 31);
    CHECK(first == repeated);
    CHECK(first.front() == doctest::Approx(0.0f));
    CHECK(first.back() == doctest::Approx(1.0f));
    CHECK(*std::max_element(first.begin(), first.end()) > 1.0f);
    CHECK(*std::max_element(first.begin(), first.end()) <= 1.5f);
    CHECK(*std::min_element(first.begin(), first.end()) >= -0.5f);
}

TEST_CASE("render plan spring bake rejects invalid segment and sampling inputs") {
    using namespace chronon3d;
    using namespace chronon3d::render_plan;
    CHECK_THROWS_AS(bake_spring_progress(Spring::Gentle, 0.0, 30, 1),
                    std::invalid_argument);
    CHECK_THROWS_AS(bake_spring_progress(Spring::Gentle, 1.0, 0, 1),
                    std::invalid_argument);
    CHECK_THROWS_AS(bake_spring_progress(Spring::Gentle, 1.0, 30, 1, 17),
                    std::invalid_argument);
    CHECK_THROWS_AS(bake_spring_progress(
                        SpringConfig{10001.0f, 100.0f, 15.0f, 0.0f}, 1.0, 30, 1),
                    std::invalid_argument);
}

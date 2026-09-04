#include <doctest/doctest.h>

#include "utils/common/cli_mappers.hpp"

TEST_CASE("CLI typed config parsers preserve canonical runtime values") {
    using namespace chronon3d;
    using namespace chronon3d::cli;

    CHECK(parse_backend_preference("auto") == graph::BackendPreference::Auto);
    CHECK(parse_backend_preference("software") == graph::BackendPreference::Software);
    CHECK(parse_backend_preference("vulkan") == graph::BackendPreference::GPU);

    CHECK(parse_motion_blur_mode(0) == MotionBlurMode::Off);
    CHECK(parse_motion_blur_mode(1) == MotionBlurMode::TemporalAccumulation);
    CHECK(parse_motion_blur_mode(2) == MotionBlurMode::VelocityApproximation);

    CHECK(parse_motion_blur_pattern(0) == TemporalSamplePattern::Uniform);
    CHECK(parse_motion_blur_pattern(1) == TemporalSamplePattern::Stratified);
    CHECK(parse_motion_blur_pattern(2) == TemporalSamplePattern::Halton);

    CHECK(parse_motion_blur_filter(0) == TemporalFilter::Box);
    CHECK(parse_motion_blur_filter(1) == TemporalFilter::Triangle);
    CHECK(parse_motion_blur_filter(2) == TemporalFilter::Gaussian);
}

#include <doctest/doctest.h>
#include <apps/chronon3d_cli/utils/cli_utils.hpp>

using namespace chronon3d::cli;

TEST_CASE("parse single frame") {
    auto r = parse_frames_safe("12");
    CHECK(r.ok);
    CHECK(r.value.start == 12);
    CHECK(r.value.end == 12);
    CHECK(r.value.step == 1);
}

TEST_CASE("parse frame range") {
    auto r = parse_frames_safe("0-90");
    CHECK(r.ok);
    CHECK(r.value.start == 0);
    CHECK(r.value.end == 90);
    CHECK(r.value.step == 1);
}

TEST_CASE("parse stepped frame range") {
    auto r = parse_frames_safe("0-90x5");
    CHECK(r.ok);
    CHECK(r.value.start == 0);
    CHECK(r.value.end == 90);
    CHECK(r.value.step == 5);
}

TEST_CASE("reject invalid frame ranges") {
    CHECK_FALSE(parse_frames_safe("").ok);
    CHECK_FALSE(parse_frames_safe("abc").ok);
    CHECK_FALSE(parse_frames_safe("0-").ok);
    CHECK_FALSE(parse_frames_safe("100-0").ok);
    CHECK_FALSE(parse_frames_safe("0-90x0").ok);
    CHECK_FALSE(parse_frames_safe("0-90x-1").ok);
}

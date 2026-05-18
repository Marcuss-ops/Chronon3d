#include <doctest/doctest.h>
#include <apps/chronon3d_cli/utils/frame_chunks.hpp>

using namespace chronon3d;
using namespace chronon3d::cli;

TEST_CASE("split_frame_range returns empty for invalid ranges") {
    CHECK(split_frame_range(0, 0, 1).empty());
    CHECK(split_frame_range(10, 0, 1).empty());
    CHECK(split_frame_range(0, 10, 0).empty());
    CHECK(split_frame_range(0, 10, -1).empty());
}

TEST_CASE("split_frame_range keeps one chunk unchanged") {
    auto chunks = split_frame_range(0, 10, 1);

    REQUIRE(chunks.size() == 1);
    CHECK(chunks[0].start == 0);
    CHECK(chunks[0].end == 10);
}

TEST_CASE("split_frame_range distributes remainder to first chunks") {
    auto chunks = split_frame_range(0, 10, 3);

    REQUIRE(chunks.size() == 3);

    CHECK(chunks[0].start == 0);
    CHECK(chunks[0].end == 4);

    CHECK(chunks[1].start == 4);
    CHECK(chunks[1].end == 7);

    CHECK(chunks[2].start == 7);
    CHECK(chunks[2].end == 10);
}

TEST_CASE("split_frame_range clamps chunks to total frame count") {
    auto chunks = split_frame_range(0, 3, 10);

    REQUIRE(chunks.size() == 3);

    CHECK(chunks[0].start == 0);
    CHECK(chunks[0].end == 1);

    CHECK(chunks[1].start == 1);
    CHECK(chunks[1].end == 2);

    CHECK(chunks[2].start == 2);
    CHECK(chunks[2].end == 3);
}

TEST_CASE("split_frame_range works with non-zero start frame") {
    auto chunks = split_frame_range(10, 20, 4);

    REQUIRE(chunks.size() == 4);

    CHECK(chunks[0].start == 10);
    CHECK(chunks[0].end == 13);

    CHECK(chunks[1].start == 13);
    CHECK(chunks[1].end == 16);

    CHECK(chunks[2].start == 16);
    CHECK(chunks[2].end == 18);

    CHECK(chunks[3].start == 18);
    CHECK(chunks[3].end == 20);
}

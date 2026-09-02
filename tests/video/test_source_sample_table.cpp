#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/media/video/source_sample_table.hpp>

using chronon3d::Rational;
using chronon3d::RationalTime;
using chronon3d::media::SourceSample;
using chronon3d::media::SourceSampleTable;

TEST_CASE("source sample table selects VFR samples by exact PTS coverage") {
    SourceSampleTable table{Rational{1, 1000}};
    table.add(SourceSample{.pts=0, .duration=40, .source_order=0, .keyframe=true});
    table.add(SourceSample{.pts=40, .duration=20, .source_order=1});
    table.add(SourceSample{.pts=60, .duration=60, .source_order=2});
    table.finalize();

    REQUIRE(table.select_covering(RationalTime{39, {1, 1000}}));
    CHECK(*table.select_covering(RationalTime{39, {1, 1000}}) == 0);
    CHECK(*table.select_covering(RationalTime{40, {1, 1000}}) == 1);
    CHECK(*table.select_covering(RationalTime{59, {1, 1000}}) == 1);
    CHECK(*table.select_covering(RationalTime{60, {1, 1000}}) == 2);
    CHECK_FALSE(table.select_covering(RationalTime{120, {1, 1000}}));
}

TEST_CASE("source sample table resolves exact rational times without FPS") {
    SourceSampleTable table{Rational{1, 90000}};
    table.add(SourceSample{.pts=30000, .duration=3000, .source_order=0, .keyframe=true});
    table.finalize();
    const auto selected = table.select_covering(RationalTime{1, {1, 3}});
    REQUIRE(selected);
    CHECK(*selected == 0);
}

TEST_CASE("duplicate PTS uses deterministic source-order tie break") {
    SourceSampleTable table{Rational{1, 1000}};
    table.add(SourceSample{.pts=0, .duration=40, .source_order=9});
    table.add(SourceSample{.pts=0, .duration=40, .source_order=3, .keyframe=true});
    table.finalize();
    const auto selected = table.select_covering(RationalTime{0, {1, 1000}});
    REQUIRE(selected);
    CHECK(table[*selected].source_order == 3);
    CHECK(table.pts_ordinal(0) == 0);
    CHECK(table.pts_ordinal(1) == 1);
}

TEST_CASE("source sample table preserves presentation gaps") {
    SourceSampleTable table{Rational{1, 1000}};
    table.add(SourceSample{.pts=0, .duration=10, .source_order=0});
    table.add(SourceSample{.pts=20, .duration=10, .source_order=1});
    table.finalize();
    CHECK_FALSE(table.select_covering(RationalTime{15, {1, 1000}}));
}

TEST_CASE("continuity boundaries invalidate sequential decode") {
    SourceSampleTable table{Rational{1, 1000}};
    table.add(SourceSample{.pts=0, .duration=10, .source_order=0, .continuity_id=0, .keyframe=true});
    table.add(SourceSample{.pts=10, .duration=10, .source_order=1, .continuity_id=0});
    table.add(SourceSample{.pts=20, .duration=10, .source_order=2, .continuity_id=1, .keyframe=true});
    table.finalize();
    CHECK(table.are_sequential(0, 1));
    CHECK_FALSE(table.are_sequential(1, 2));
    REQUIRE(table.previous_keyframe(1));
    CHECK(*table.previous_keyframe(1) == 0);
    REQUIRE(table.previous_keyframe(2));
    CHECK(*table.previous_keyframe(2) == 2);
}

TEST_CASE("missing sample durations are inferred from PTS deltas not nominal FPS") {
    SourceSampleTable table{Rational{1, 1000}};
    table.add(SourceSample{.pts=0, .duration=0, .source_order=0});
    table.add(SourceSample{.pts=33, .duration=0, .source_order=1});
    table.add(SourceSample{.pts=70, .duration=20, .source_order=2});
    table.finalize();
    CHECK(table[0].duration == 33);
    CHECK(table[1].duration == 37);
    CHECK(table[2].duration == 20);
}

TEST_CASE("single sample duration falls back to one source tick") {
    SourceSampleTable table{Rational{1, 48000}};
    table.add(SourceSample{.pts=123, .duration=0, .source_order=0});
    table.finalize();
    CHECK(table[0].duration == 1);
}

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "chronon/Chronon.hpp"

#include <chrono>

using namespace std::chrono_literals;

TEST_CASE("lerp interpolates linearly") {
    CHECK(ch::lerp(0.0, 10.0, 0.0) == doctest::Approx(0.0));
    CHECK(ch::lerp(0.0, 10.0, 0.5) == doctest::Approx(5.0));
    CHECK(ch::lerp(10.0, 20.0, 1.0) == doctest::Approx(20.0));
}

TEST_CASE("progress clamps inside the frame window") {
    ch::Frame frame;
    frame.t = 1.0;

    CHECK(frame.progress(0s, 2s) == doctest::Approx(0.5));
    CHECK(frame.progress(1s, 3s) == doctest::Approx(0.0));
    CHECK(frame.progress(2s, 2s) == doctest::Approx(0.0));
}

TEST_CASE("layers stay declarative and sortable by depth") {
    auto front_layer = ch::Solid({1.0, 0.0, 0.0, 1.0}).z(100.0);
    auto back_layer = ch::Video(ASSET("video.mp4")).z(-200.0);
    auto center_layer = ch::Text("middle").z(0.0);

    const auto composition = ch::Compose(front_layer, back_layer, center_layer);
    const auto ordered = composition.sortedLayers();

    REQUIRE(ordered.size() == 3);
    CHECK(ordered[0].pos.z == doctest::Approx(-200.0));
    CHECK(ordered[1].pos.z == doctest::Approx(0.0));
    CHECK(ordered[2].pos.z == doctest::Approx(100.0));
}

TEST_CASE("render job keeps timeline metadata lightweight") {
    const ch::Scene intro = ch::Scene([](ch::Frame) {
        return ch::Compose(ch::Text("intro"));
    }).duration(5s);

    const ch::Scene outro = ch::Scene([](ch::Frame) {
        return ch::Compose(ch::Text("outro"));
    }).duration(3s).transition(ch::CrossFade(500ms));

    const ch::RenderJob job{
        .size = {1920, 1080},
        .fps = 60,
        .scenes = {intro, outro},
        .output = "video.mp4"
    };

    CHECK(job.size.width == 1920);
    CHECK(job.size.height == 1080);
    CHECK(job.fps == 60);
    REQUIRE(job.scenes.size() == 2);
    CHECK(job.scenes[0].duration().count() == doctest::Approx(5.0));
    CHECK(job.scenes[1].duration().count() == doctest::Approx(3.0));
    REQUIRE(job.scenes[1].transition().has_value());
    CHECK(job.scenes[1].transition()->duration.count() == doctest::Approx(0.5));
}

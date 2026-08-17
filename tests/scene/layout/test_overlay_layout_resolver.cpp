// ─── test_overlay_layout_resolver.cpp — V1 overlay layout resolver ─────────
//
// Locks the greedy deterministic contract of
// `chronon3d::layout::OverlayLayoutResolver`: intent → anchor mapping,
// safe-area containment, occupied-region collision avoidance, fallback
// intents, priority ordering, and preflight failure (invalid + warning).

#include <doctest/doctest.h>

#include <chronon3d/layout/overlay_layout_resolver.hpp>

using namespace chronon3d;
using namespace chronon3d::layout;

TEST_CASE("OverlayLayoutResolver: intent_anchor maps the closed vocabulary") {
    CHECK(intent_anchor("center") == Anchor::Center);
    CHECK(intent_anchor("safe_area") == Anchor::Center);
    CHECK(intent_anchor("lower_third") == Anchor::BottomCenter);
    CHECK(intent_anchor("lower_left") == Anchor::BottomLeft);
    CHECK(intent_anchor("lower_right") == Anchor::BottomRight);
    CHECK(intent_anchor("top_left") == Anchor::TopLeft);
    CHECK(intent_anchor("top_right") == Anchor::TopRight);
    CHECK(intent_anchor("bottom_left") == Anchor::BottomLeft);
    CHECK(intent_anchor("bottom_right") == Anchor::BottomRight);
    CHECK(intent_anchor("image_left") == Anchor::MiddleLeft);
    CHECK(intent_anchor("image_right") == Anchor::MiddleRight);
    CHECK(intent_anchor("unknown") == Anchor::Center);
}

TEST_CASE("OverlayLayoutResolver: single center request resolves to a centered box") {
    OverlayLayoutRequest req;
    req.id = "title";
    req.intent = "center";
    req.width = 540.0f;
    req.height = 104.0f;
    req.safe_margin = 0.0f;  // no inset → exact center math

    const auto results = OverlayLayoutResolver{}.solve(1920.0f, 1080.0f, {req});
    REQUIRE(results.size() == 1);
    CHECK(results[0].valid);
    CHECK(results[0].intent == "center");
    CHECK(results[0].x == doctest::Approx(960.0f - 270.0f));
    CHECK(results[0].y == doctest::Approx(540.0f - 52.0f));
}

TEST_CASE("OverlayLayoutResolver: colliding lower_left falls back to lower_right") {
    OverlayLayoutRequest base;
    base.intent = "lower_left";
    base.fallback_intents = {"lower_right"};
    base.width = 400.0f;
    base.height = 100.0f;
    base.safe_margin = 0.0f;

    OverlayLayoutRequest first = base;
    first.id = "person_a";
    OverlayLayoutRequest second = base;
    second.id = "person_b";

    const auto results =
        OverlayLayoutResolver{}.solve(1920.0f, 1080.0f, {first, second});
    REQUIRE(results.size() == 2);
    CHECK(results[0].valid);
    CHECK(results[1].valid);
    CHECK(results[0].intent == "lower_left");
    CHECK(results[1].intent == "lower_right");
    CHECK(results[0].x == doctest::Approx(0.0f));
    CHECK(results[1].x == doctest::Approx(1920.0f - 400.0f));
}

TEST_CASE("OverlayLayoutResolver: box larger than the safe area is invalid with warning") {
    OverlayLayoutRequest req;
    req.id = "oversized";
    req.intent = "center";
    req.width = 1920.0f;
    req.height = 1080.0f;
    req.safe_margin = 0.06f;

    const auto results = OverlayLayoutResolver{}.solve(1920.0f, 1080.0f, {req});
    REQUIRE(results.size() == 1);
    CHECK_FALSE(results[0].valid);
    CHECK_FALSE(results[0].warning.empty());
}

TEST_CASE("OverlayLayoutResolver: pre-seeded occupied region forces a fallback") {
    OverlayLayoutRequest req;
    req.id = "text";
    req.intent = "lower_third";
    req.fallback_intents = {"top_left"};
    req.width = 540.0f;
    req.height = 104.0f;
    req.safe_margin = 0.0f;

    // Occupy the bottom-center slot where lower_third would land.
    OverlayRegion occupied;
    occupied.x = 960.0f - 270.0f;
    occupied.y = 1080.0f - 104.0f;
    occupied.width = 540.0f;
    occupied.height = 104.0f;

    const auto results =
        OverlayLayoutResolver{}.solve(1920.0f, 1080.0f, {req}, {occupied});
    REQUIRE(results.size() == 1);
    CHECK(results[0].valid);
    CHECK(results[0].intent == "top_left");
    CHECK(results[0].x == doctest::Approx(0.0f));
    CHECK(results[0].y == doctest::Approx(0.0f));
}

TEST_CASE("OverlayLayoutResolver: higher priority resolves first and takes the slot") {
    OverlayLayoutRequest low;
    low.id = "low";
    low.intent = "lower_third";
    low.width = 540.0f;
    low.height = 104.0f;
    low.priority = 1.0f;

    OverlayLayoutRequest high = low;
    high.id = "high";
    high.priority = 10.0f;

    const auto results =
        OverlayLayoutResolver{}.solve(1920.0f, 1080.0f, {low, high});
    REQUIRE(results.size() == 2);
    CHECK(results[0].id == "low");
    CHECK(results[1].id == "high");
    // high priority landed on the preferred slot; low had no fallback and
    // therefore reports a preflight failure.
    CHECK(results[1].valid);
    CHECK(results[1].intent == "lower_third");
    CHECK_FALSE(results[0].valid);
}

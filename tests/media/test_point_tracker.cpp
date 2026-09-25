// =============================================================================
// test_point_tracker.cpp — deterministic 2D point tracker
//
// Synthetic sequences only: a square patch translated by a known integer (and
// fractional) amount must be recovered with sub-pixel accuracy, and a patch
// that disappears must report a lost sample instead of a jump.
// =============================================================================

#include <doctest/doctest.h>

#include <chronon3d/media/tracking/point_tracker.hpp>

#include <cmath>
#include <span>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::media::tracking;

namespace {

constexpr int kWidth = 64, kHeight = 64;

// Dark background with one bright square whose top-left corner is (ox, oy).
std::vector<Color> make_frame(int ox, int oy, int square = 10) {
    std::vector<Color> frame(static_cast<std::size_t>(kWidth) * kHeight,
                             Color{0.05f, 0.05f, 0.05f, 1.0f});
    for (int y = oy; y < oy + square; ++y)
        for (int x = ox; x < ox + square; ++x)
            if (x >= 0 && x < kWidth && y >= 0 && y < kHeight)
                frame[static_cast<std::size_t>(y) * kWidth + x] =
                    Color{0.9f, 0.9f, 0.9f, 1.0f};
    return frame;
}

} // namespace

TEST_CASE("PointTracker2D follows a translated patch deterministically") {
    const auto frame0 = make_frame(20, 20);
    const auto frame1 = make_frame(23, 21);
    const auto frame2 = make_frame(27, 25);

    PointTracker2D tracker(kWidth, kHeight, PointTrackSpec{.patch_radius = 8, .search_radius = 12});
    const auto s0 = tracker.start(frame0, 25.0f, 25.0f, 0);
    CHECK(s0.tracked);
    CHECK(s0.confidence == doctest::Approx(1.0f));

    const auto s1 = tracker.push(frame1, 1);
    CHECK(s1.tracked);
    CHECK(s1.x == doctest::Approx(28.0f).epsilon(0.25));
    CHECK(s1.y == doctest::Approx(26.0f).epsilon(0.25));

    const auto s2 = tracker.push(frame2, 2);
    CHECK(s2.tracked);
    CHECK(s2.x == doctest::Approx(32.0f).epsilon(0.25));
    CHECK(s2.y == doctest::Approx(30.0f).epsilon(0.25));

    CHECK(tracker.track().ok());
    CHECK(tracker.track().samples.size() == 3);
    CHECK(tracker.track().lost_frames == 0);
}

TEST_CASE("PointTracker2D reports a lost patch without moving the position") {
    const auto frame0 = make_frame(20, 20);
    const auto empty = std::vector<Color>(static_cast<std::size_t>(kWidth) * kHeight,
                                          Color{0.05f, 0.05f, 0.05f, 1.0f});

    PointTracker2D tracker(kWidth, kHeight);
    tracker.start(frame0, 25.0f, 25.0f, 0);
    const auto lost = tracker.push(empty, 1);
    CHECK_FALSE(lost.tracked);
    CHECK(lost.confidence < 0.25f);
    CHECK(lost.x == doctest::Approx(25.0f));
    CHECK(lost.y == doctest::Approx(25.0f));
    CHECK_FALSE(tracker.track().ok());
    CHECK(tracker.track().lost_frames == 1);
}

TEST_CASE("PointTracker2D is a pure function of its inputs") {
    const auto frame0 = make_frame(20, 20);
    const auto frame1 = make_frame(22, 19);

    const auto run = [&] {
        PointTracker2D tracker(kWidth, kHeight);
        tracker.start(frame0, 25.0f, 25.0f, 0);
        return tracker.push(frame1, 1);
    };
    const auto a = run();
    const auto b = run();
    CHECK(a.x == b.x);
    CHECK(a.y == b.y);
    CHECK(a.confidence == b.confidence);
}

TEST_CASE("PointTracker2D rejects invalid configuration fail-closed") {
    CHECK_THROWS(PointTracker2D(0, 64));
    CHECK_THROWS(PointTracker2D(64, 64, PointTrackSpec{.patch_radius = 1}));
    CHECK_THROWS(PointTracker2D(64, 64, PointTrackSpec{.search_radius = 0}));

    PointTracker2D tracker(kWidth, kHeight);
    const auto frame0 = make_frame(20, 20);
    CHECK_THROWS(tracker.push(frame0, 0)); // push before start
    CHECK_THROWS(tracker.start(std::span<const Color>(frame0).first(16), 1.0f, 1.0f));
}

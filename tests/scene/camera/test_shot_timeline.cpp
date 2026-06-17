// ==============================================================================
// tests/scene/camera/test_shot_timeline.cpp
//
// PR7 — ShotTimeline tests.
//
// Tests:
//   1. Empty timeline returns empty camera
//   2. Add shot and find at frame
//   3. Shot pair returns current + next for transition overlap
//   4. Cut transition is instant
//   5. SmoothBlend interpolates position/rotation
//   6. Push transition uses ease-out
//   7. WhipPan transition uses cubic smoothstep on rotation
//   8. FocusHandoff transitions focus_distance
//   9. Overlap boundary produces non-NaN camera
//  10. TransitionRegistry register + create
// ==============================================================================
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/scene/camera/camera_v1/shot_timeline.hpp>

#include <cmath>

namespace {

using namespace chronon3d::camera_v1;

inline bool approx(float a, float b, float tol = 1e-4f) {
    return std::abs(a - b) <= tol;
}

// ==============================================================================
// 1 — Empty timeline.
// ==============================================================================
TEST_CASE("PR7: empty timeline returns empty camera") {
    auto timeline = std::make_shared<ShotTimeline>();
    ShotTimelineResolver resolver(timeline);

    ConstraintSession session;
    auto cam = resolver.evaluate(0, session);
    // Empty camera = zero position.
    CHECK(approx(cam.position.x, 0.0f));
    CHECK(approx(cam.position.y, 0.0f));
    CHECK(approx(cam.position.z, 0.0f));
}

// ==============================================================================
// 2 — Add shot and find.
// ==============================================================================
TEST_CASE("PR7: add shot and find at frame") {
    auto timeline = std::make_shared<ShotTimeline>();
    CameraShot shot;
    shot.name = "test-shot";
    shot.start_frame = 0;
    shot.end_frame = 30;
    timeline->add_shot(std::move(shot));

    CHECK(timeline->size() == 1);
    CHECK(timeline->find_shot(15) != nullptr);
    CHECK(timeline->find_shot(15)->name == "test-shot");
    CHECK(timeline->find_shot(50) == nullptr);
}

// ==============================================================================
// 3 — Shot pair.
// ==============================================================================
TEST_CASE("PR7: shot pair returns current and next for overlap") {
    auto timeline = std::make_shared<ShotTimeline>();
    CameraShot s1, s2;
    s1.name = "first";  s1.start_frame = 0;  s1.end_frame = 30;
    s2.name = "second"; s2.start_frame = 30; s2.end_frame = 60;
    timeline->add_shot(std::move(s1));
    timeline->add_shot(std::move(s2));

    auto pair = timeline->find_pair(25);
    CHECK(pair.current != nullptr);
    CHECK(pair.next != nullptr);
    CHECK(pair.current->name == "first");
    CHECK(pair.next->name == "second");
}

// ==============================================================================
// 4 — Cut transition is instant.
// ==============================================================================
TEST_CASE("PR7: cut transition is instant") {
    Camera2_5D from, to;
    from.position = {0, 0, -1000};
    to.position   = {500, 0, -1000};

    ShotTimelineResolver resolver(nullptr);
    // Cut at t=0.5 should still return from (cuts happen at t=1).
    // We test the transition directly.
    auto cut = resolver.default_cut();
    auto result = cut->evaluate(0.99f, from, to);
    // At t<1, cut returns empty (no-op). At t=1, cut returns `to`.
    // Verify that at t=1 we get `to`.
    result = cut->evaluate(1.0f, from, to);
    CHECK(approx(result.position.x, 500.0f));
}

// ==============================================================================
// 5 — SmoothBlend interpolates position.
// ==============================================================================
TEST_CASE("PR7: smooth blend interpolates position") {
    Camera2_5D from, to;
    from.position = {0, 0, -1000};
    to.position   = {400, 0, -1000};

    ShotTimelineResolver resolver(nullptr);
    auto blend = resolver.default_smooth_blend();

    auto mid = blend->evaluate(0.5f, from, to);
    CHECK(approx(mid.position.x, 200.0f, 1.0f));

    auto end = blend->evaluate(1.0f, from, to);
    CHECK(approx(end.position.x, 400.0f, 1.0f));
}

// ==============================================================================
// 6 — Push transition (ease-out).
// ==============================================================================
TEST_CASE("PR7: push transition uses ease-out") {
    Camera2_5D from, to;
    from.position = {0, 0, -1000};
    to.position   = {400, 0, -1000};

    ShotTimelineResolver resolver(nullptr);
    auto push = resolver.default_push();

    // Push at t=0.5: ease-out gives t*(2-t) = 0.5*1.5 = 0.75
    auto mid = push->evaluate(0.5f, from, to);
    // Position should be beyond linear midpoint (due to ease-out overshoot feel).
    CHECK(mid.position.x > 200.0f);
    CHECK(mid.position.x < 400.0f);
}

// ==============================================================================
// 7 — WhipPan transition.
// ==============================================================================
TEST_CASE("PR7: whip pan interpolates rotation") {
    Camera2_5D from, to;
    from.rotation = {0, 0, 0};
    to.rotation   = {0, 90, 0};

    ShotTimelineResolver resolver(nullptr);
    auto whip = resolver.default_whip_pan();

    auto mid = whip->evaluate(0.5f, from, to);
    // Whip pan rotates from→to, position stays.
    CHECK(approx(mid.position.x, 0.0f));
    CHECK(mid.rotation.y > 0.0f);
    CHECK(mid.rotation.y < 90.0f);
}

// ==============================================================================
// 8 — FocusHandoff.
// ==============================================================================
TEST_CASE("PR7: focus handoff transitions focus distance") {
    Camera2_5D from, to;
    from.focus_distance = 100.0f;
    to.focus_distance   = 900.0f;

    ShotTimelineResolver resolver(nullptr);
    auto fh = resolver.default_focus_handoff();

    auto mid = fh->evaluate(0.5f, from, to);
    CHECK(approx(mid.focus_distance, 500.0f, 50.0f));
}

// ==============================================================================
// 9 — Overlap boundary non-NaN.
// ==============================================================================
TEST_CASE("PR7: overlap boundary produces non-NaN camera") {
    auto timeline = std::make_shared<ShotTimeline>();
    CameraShot s1, s2;
    s1.name = "first";  s1.start_frame = 0;  s1.end_frame = 30;
    s1.transition_in = CameraTransitionKind::SmoothBlend;
    s1.transition_frames = 10;
    s2.name = "second"; s2.start_frame = 30; s2.end_frame = 60;
    timeline->add_shot(std::move(s1));
    timeline->add_shot(std::move(s2));

    ShotTimelineResolver resolver(timeline);
    ConstraintSession session;
    auto cam = resolver.evaluate(25, session);  // in overlap zone

    CHECK_FALSE(std::isnan(cam.position.x));
    CHECK_FALSE(std::isnan(cam.position.y));
    CHECK_FALSE(std::isnan(cam.position.z));
}

// ==============================================================================
// Test cut transition (defined before use).
// ==============================================================================
namespace {
class TestCutTransition : public CameraTransition {
public:
    std::string id() const override { return "test.cut"; }
    Camera2_5D evaluate(float t, const Camera2_5D&, const Camera2_5D& to) const override {
        return (t >= 1.0f) ? to : Camera2_5D{};
    }
};
} // namespace

// ==============================================================================
// 10 — TransitionRegistry register + create.
// ==============================================================================
TEST_CASE("PR7: transition registry register and create") {
    auto& reg = CameraTransitionRegistry::instance();
    reg.register_transition(CameraTransitionKind::Cut,
        []{ return std::make_shared<TestCutTransition>(); });

    CHECK(reg.has(CameraTransitionKind::Cut));
    auto t = reg.create(CameraTransitionKind::Cut);
    CHECK(t != nullptr);
}

} // namespace

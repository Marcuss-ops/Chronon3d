// ==============================================================================
// tests/scene/camera/test_shot_timeline.cpp
//
// ShotTimeline tests — migrated to current API (June 2026).
//
// Tests:
//   1. Empty timeline returns empty camera
//   2. Add shot and find at frame
//   3. Shot pair returns current + next for transition overlap
//   4. Cut transition shows 'from' until t=1
//   5. SmoothBlend interpolates position (quaternion slerp rotation)
//   6. Push transition uses ease-out
//   7. WhipPan transition interpolates rotation via quaternion
//   8. FocusHandoff transitions focus distance
//   9. Overlap boundary produces non-NaN camera
//  10. TransitionRegistry register + create + freeze
//  11. Endpoint parity: t=0==from, t=1==to
//  12. Validation catches negative duration
// ==============================================================================
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <tests/helpers/test_math.hpp>

#include <chronon3d/scene/camera/camera_v1/shot_timeline.hpp>

#include <cmath>
using namespace chronon3d;

namespace {

using namespace chronon3d::camera_v1;
using chronon3d::test::approx;

// ==============================================================================
// 1 — Empty timeline.
// ==============================================================================
TEST_CASE("PR7: empty timeline returns empty camera") {
    auto timeline = std::make_shared<ShotTimeline>();
    ShotTimelineResolver resolver(timeline);

    ShotTimelineSession tls;
    auto cam = resolver.evaluate(0, tls);
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
    bool ok = timeline->add_shot(std::move(shot));
    CHECK(ok);
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
// 4 — Cut transition shows 'from' until t=1, then 'to'.
// ==============================================================================
TEST_CASE("PR7: cut transition shows from until t=1") {
    Camera2_5D from, to;
    from.position = {0, 0, -1000};
    to.position   = {500, 0, -1000};

    auto cut = ShotTimelineResolver::default_cut();

    auto r0 = cut->evaluate(0.0f, from, to);
    CHECK(approx(r0.position.x, 0.0f));

    auto rh = cut->evaluate(0.5f, from, to);
    CHECK(approx(rh.position.x, 0.0f));

    auto r1 = cut->evaluate(1.0f, from, to);
    CHECK(approx(r1.position.x, 500.0f));
}

// ==============================================================================
// 5 — SmoothBlend interpolates position.
// ==============================================================================
TEST_CASE("PR7: smooth blend interpolates position") {
    Camera2_5D from, to;
    from.position = {0, 0, -1000};
    to.position   = {400, 0, -1000};

    auto blend = ShotTimelineResolver::default_smooth_blend();

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

    auto push = ShotTimelineResolver::default_push();

    auto mid = push->evaluate(0.5f, from, to);
    CHECK(mid.position.x > 200.0f);
    CHECK(mid.position.x < 400.0f);
}

// ==============================================================================
// 7 — WhipPan transition.
// ==============================================================================
TEST_CASE("PR7: whip pan interpolates rotation via quaternion") {
    Camera2_5D from, to;
    from.rotation = {0, 0, 0};
    to.rotation   = {0, 90, 0};

    auto whip = ShotTimelineResolver::default_whip_pan();

    auto mid = whip->evaluate(0.5f, from, to);
    CHECK(mid.rotation.y > 0.0f);
    CHECK(mid.rotation.y < 90.0f);
}

// ==============================================================================
// 8 — FocusHandoff.
// ==============================================================================
TEST_CASE("PR7: focus handoff transitions focus distance") {
    Camera2_5D from, to;
    from.dof.focus_distance = 100.0f;
    to.dof.focus_distance   = 900.0f;

    auto fh = ShotTimelineResolver::default_focus_handoff();

    auto mid = fh->evaluate(0.5f, from, to);
    CHECK(approx(mid.dof.focus_distance, 500.0f, 50.0f));
}

// ==============================================================================
// 9 — Overlap boundary non-NaN.
// ==============================================================================
TEST_CASE("PR7: overlap boundary produces non-NaN camera") {
    auto timeline = std::make_shared<ShotTimeline>();
    CameraShot s1, s2;
    s1.name = "first";  s1.start_frame = 0;  s1.end_frame = 30;
    s1.transition_out = CameraTransitionKind::SmoothBlend;
    s1.transition_frames = 10;
    s2.name = "second"; s2.start_frame = 30; s2.end_frame = 60;
    timeline->add_shot(std::move(s1));
    timeline->add_shot(std::move(s2));

    ShotTimelineResolver resolver(timeline);
    ShotTimelineSession tls;
    auto cam = resolver.evaluate(25, tls);

    CHECK_FALSE(std::isnan(cam.position.x));
    CHECK_FALSE(std::isnan(cam.position.y));
    CHECK_FALSE(std::isnan(cam.position.z));
}

// ==============================================================================
// 10 — TransitionRegistry register + create + freeze.
// ==============================================================================
TEST_CASE("PR7: transition registry register, create, and freeze") {
    auto& reg = CameraTransitionRegistry::instance();

    reg.register_transition(CameraTransitionKind::Cut,
        ShotTimelineResolver::default_cut);

    CHECK(reg.has(CameraTransitionKind::Cut));
    auto t = reg.create(CameraTransitionKind::Cut);
    CHECK(t != nullptr);

    reg.freeze();
    CHECK(reg.is_frozen());
}

// ==============================================================================
// 11 — Endpoint parity: t=0 == from_cam, t=1 == to_cam.
// ==============================================================================
TEST_CASE("PR7: endpoint parity for all transitions") {
    Camera2_5D from, to;
    from.position = {10, 20, -100};
    from.rotation = {5, 10, 2};
    from.fov_deg = 45.0f;
    from.zoom = 1.5f;
    from.dof.focus_distance = 200.0f;
    to.position = {100, 200, -500};
    to.rotation = {30, 80, 15};
    to.fov_deg = 60.0f;
    to.zoom = 2.0f;
    to.dof.focus_distance = 800.0f;

    auto transitions = {
        ShotTimelineResolver::default_cut(),
        ShotTimelineResolver::default_smooth_blend(),
        ShotTimelineResolver::default_push(),
        ShotTimelineResolver::default_whip_pan(),
        ShotTimelineResolver::default_focus_handoff(),
    };

    for (auto& t : transitions) {
        auto r0 = t->evaluate(0.0f, from, to);
        auto r1 = t->evaluate(1.0f, from, to);

        CHECK(approx(r0.position.x, from.position.x, 1.0f));
        CHECK(approx(r0.position.y, from.position.y, 1.0f));
        CHECK(approx(r0.position.z, from.position.z, 1.0f));

        CHECK(approx(r1.position.x, to.position.x, 1.0f));
        CHECK(approx(r1.position.y, to.position.y, 1.0f));
        CHECK(approx(r1.position.z, to.position.z, 1.0f));
    }
}

// ==============================================================================
// 12 — Validation catches zero/negative duration.
// ==============================================================================
TEST_CASE("PR7: validation catches zero/negative duration") {
    auto timeline = std::make_shared<ShotTimeline>();
    CameraShot bad;
    bad.name = "bad-shot";
    bad.start_frame = 10;
    bad.end_frame = 10;
    CHECK_FALSE(timeline->add_shot(bad));

    bad.end_frame = 5;
    CHECK_FALSE(timeline->add_shot(bad));
}

} // namespace

#include <doctest/doctest.h>
#include <chronon3d/animation/core/animation_track.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/math/glm_types.hpp>

using namespace chronon3d;

// ── AnimationTrack<T> construction ──────────────────────────────────────
TEST_CASE("AnimationTrack: from().to() builds ordered keyframes") {
    auto track = AnimationTrack<float>()
        .from(Frame{0},   0.0f, Easing::Linear)
        .to  (Frame{60},  60.0f, Easing::Linear)
        .to  (Frame{120}, 120.0f);

    CHECK(track.size() == 3);
    CHECK_FALSE(track.empty());

    const auto& keys = track.keys();
    CHECK(keys[0].frame == Frame{0});
    CHECK(keys[0].value == doctest::Approx(0.0f));
    CHECK(keys[1].frame == Frame{60});
    CHECK(keys[1].value == doctest::Approx(60.0f));
    CHECK(keys[2].frame == Frame{120});
    CHECK(keys[2].value == doctest::Approx(120.0f));
}

TEST_CASE("AnimationTrack: .at() is alias for .to()") {
    auto track = AnimationTrack<float>()
        .from(Frame{0},   1.0f)
        .at  (Frame{50},  0.5f, Easing::InOutSine)
        .to  (Frame{100}, 0.0f);

    CHECK(track.size() == 3);
    const auto& keys = track.keys();
    CHECK(keys[1].frame == Frame{50});
    CHECK(keys[1].value == doctest::Approx(0.5f));
}

TEST_CASE("AnimationTrack: empty track") {
    AnimationTrack<float> track;
    CHECK(track.empty());
    CHECK(track.size() == 0);
    CHECK(track.keys().empty());
}

TEST_CASE("AnimationTrack: clear() empties track") {
    auto track = AnimationTrack<float>()
        .from(Frame{0}, 1.0f)
        .to  (Frame{10}, 0.0f);

    CHECK_FALSE(track.empty());
    track.clear();
    CHECK(track.empty());
    CHECK(track.size() == 0);
}

TEST_CASE("AnimationTrack: Vec3 tracks") {
    auto track = AnimationTrack<Vec3>()
        .from(Frame{0},   Vec3{0.0f, 0.0f, 0.0f}, Easing::OutCubic)
        .to  (Frame{30},  Vec3{1.0f, 1.0f, 1.0f}, Easing::InCubic);

    CHECK(track.size() == 2);
    const auto& keys = track.keys();
    CHECK(keys[0].value.x == doctest::Approx(0.0f));
    CHECK(keys[0].value.y == doctest::Approx(0.0f));
    CHECK(keys[1].value.x == doctest::Approx(1.0f));
    CHECK(keys[1].value.y == doctest::Approx(1.0f));
}

// ── AnimatedValue<T>::apply_track ───────────────────────────────────────
TEST_CASE("AnimatedValue::apply_track bakes track into value") {
    auto track = AnimationTrack<float>()
        .from(Frame{0},   0.0f,  Easing::Linear)
        .to  (Frame{60},  60.0f, Easing::Linear)
        .to  (Frame{120}, 120.0f);

    AnimatedValue<float> v(0.0f);
    v.apply_track(track);

    CHECK(v.is_animated());
    CHECK(v.evaluate(Frame{0})   == doctest::Approx(0.0f));
    CHECK(v.evaluate(Frame{30})  == doctest::Approx(30.0f));
    CHECK(v.evaluate(Frame{60})  == doctest::Approx(60.0f));
    CHECK(v.evaluate(Frame{90})  == doctest::Approx(90.0f));
    CHECK(v.evaluate(Frame{120}) == doctest::Approx(120.0f));
}

TEST_CASE("AnimatedValue::apply_track clears previous keyframes") {
    auto track = AnimationTrack<float>()
        .from(Frame{0}, 100.0f)
        .to  (Frame{10}, 200.0f);

    AnimatedValue<float> v(0.0f);
    v.key(Frame{0}, 0.0f).key(Frame{100}, 999.0f);

    v.apply_track(track);

    // Old keyframes gone; only track keyframes remain
    CHECK(v.evaluate(Frame{0})  == doctest::Approx(100.0f));
    CHECK(v.evaluate(Frame{10}) == doctest::Approx(200.0f));
}

TEST_CASE("AnimatedValue::apply_track with easing") {
    auto track = AnimationTrack<float>()
        .from(Frame{0},  0.0f, Easing::OutQuad)
        .to  (Frame{60}, 60.0f);

    AnimatedValue<float> v(0.0f);
    v.apply_track(track);

    // OutQuad at t=0.5: t*(2-t) = 0.75 → 45
    CHECK(v.evaluate(Frame{30}) == doctest::Approx(45.0f).epsilon(0.01f));
}

TEST_CASE("AnimatedValue::apply_track works with Vec3") {
    auto track = AnimationTrack<Vec3>()
        .from(Frame{0},  Vec3{0.0f,  0.0f,  0.0f})
        .to  (Frame{60}, Vec3{60.0f, 60.0f, 60.0f});

    AnimatedValue<Vec3> v(Vec3{0.0f});
    v.apply_track(track);

    Vec3 mid = v.evaluate(Frame{30});
    CHECK(mid.x == doctest::Approx(30.0f));
    CHECK(mid.y == doctest::Approx(30.0f));
    CHECK(mid.z == doctest::Approx(30.0f));
}

// ── AnimatedValue<Vec3>::animate_x ──────────────────────────────────────
TEST_CASE("AnimatedValue::animate_x drives X from scalar track") {
    auto track = AnimationTrack<float>()
        .from(Frame{0},   0.0f)
        .to  (Frame{60},  60.0f);

    AnimatedValue<Vec3> v(Vec3{0.0f});
    v.animate_x(track);

    // Y and Z default to 1.0
    Vec3 r = v.evaluate(Frame{30});
    CHECK(r.x == doctest::Approx(30.0f));
    CHECK(r.y == doctest::Approx(1.0f));
    CHECK(r.z == doctest::Approx(1.0f));
}

TEST_CASE("AnimatedValue::animate_x with custom defaults") {
    auto track = AnimationTrack<float>()
        .from(Frame{0},  0.0f)
        .to  (Frame{100}, 100.0f);

    AnimatedValue<Vec3> v(Vec3{0.0f});
    v.animate_x(track, /*y_default=*/2.0f, /*z_default=*/3.0f);

    Vec3 r = v.evaluate(Frame{50});
    CHECK(r.x == doctest::Approx(50.0f));
    CHECK(r.y == doctest::Approx(2.0f));
    CHECK(r.z == doctest::Approx(3.0f));
}

TEST_CASE("AnimatedValue::animate_x clears previous keyframes") {
    auto track = AnimationTrack<float>()
        .from(Frame{0}, 10.0f)
        .to  (Frame{10}, 20.0f);

    AnimatedValue<Vec3> v(Vec3{0.0f});
    v.key(Frame{0}, Vec3{99.0f, 99.0f, 99.0f});

    v.animate_x(track);

    Vec3 r = v.evaluate(Frame{0});
    CHECK(r.x == doctest::Approx(10.0f));
    CHECK(r.y == doctest::Approx(1.0f));  // default
    CHECK(r.z == doctest::Approx(1.0f));  // default
}

// ── AnimatedValue<Vec3>::animate_y ──────────────────────────────────────
TEST_CASE("AnimatedValue::animate_y drives Y from scalar track") {
    auto track = AnimationTrack<float>()
        .from(Frame{0},   0.0f, Easing::OutQuad)
        .to  (Frame{60},  60.0f);

    AnimatedValue<Vec3> v(Vec3{0.0f});
    v.animate_y(track);

    Vec3 r = v.evaluate(Frame{30});
    CHECK(r.x == doctest::Approx(1.0f));  // default
    CHECK(r.y == doctest::Approx(45.0f).epsilon(0.01f));
    CHECK(r.z == doctest::Approx(1.0f));  // default
}

TEST_CASE("AnimatedValue::animate_y with custom defaults") {
    auto track = AnimationTrack<float>()
        .from(Frame{0},   0.0f)
        .to  (Frame{100}, 100.0f);

    AnimatedValue<Vec3> v(Vec3{0.0f});
    v.animate_y(track, /*x_default=*/5.0f, /*z_default=*/7.0f);

    Vec3 r = v.evaluate(Frame{50});
    CHECK(r.x == doctest::Approx(5.0f));
    CHECK(r.y == doctest::Approx(50.0f));
    CHECK(r.z == doctest::Approx(7.0f));
}

// ── AnimatedValue<Vec3>::animate_z ──────────────────────────────────────
TEST_CASE("AnimatedValue::animate_z drives Z from scalar track") {
    auto track = AnimationTrack<float>()
        .from(Frame{0},   -100.0f)
        .to  (Frame{100}, 100.0f);

    AnimatedValue<Vec3> v(Vec3{0.0f});
    v.animate_z(track);

    Vec3 r = v.evaluate(Frame{50});
    CHECK(r.x == doctest::Approx(1.0f));  // default
    CHECK(r.y == doctest::Approx(1.0f));  // default
    CHECK(r.z == doctest::Approx(0.0f));  // midpoint of -100→100
}

TEST_CASE("AnimatedValue::animate_z works in real-world depth scenario") {
    // Simulate a camera that starts far (-800 Z) and pushes in to 0.
    // The easing on the last keyframe is dead (no segment after it);
    // the active segment uses InQuad from frame 0.
    auto depth_track = AnimationTrack<float>()
        .from(Frame{0},   -800.0f, Easing::InQuad)
        .to  (Frame{120},    0.0f,  Easing::OutCubic);

    AnimatedValue<Vec3> pos(Vec3{0.0f});
    pos.animate_z(depth_track, /*x_default=*/0.0f, /*y_default=*/0.0f);

    // At frame 60 (halfway), InQuad…OutCubic gives a value in the middle.
    Vec3 r = pos.evaluate(Frame{60});
    CHECK(r.z < doctest::Approx(0.0f));    // still negative
    CHECK(r.z > doctest::Approx(-800.0f)); // but not at start

    Vec3 end = pos.evaluate(Frame{120});
    CHECK(end.z == doctest::Approx(0.0f));
    CHECK(end.x == doctest::Approx(0.0f));
    CHECK(end.y == doctest::Approx(0.0f));
}

// ── Multiple tracks, single AnimatedValue ───────────────────────────────
TEST_CASE("AnimatedValue: apply_track then re-apply with different track") {
    auto trackA = AnimationTrack<float>()
        .from(Frame{0}, 0.0f)
        .to  (Frame{10}, 10.0f);

    auto trackB = AnimationTrack<float>()
        .from(Frame{0}, 100.0f)
        .to  (Frame{10}, 200.0f);

    AnimatedValue<float> v(0.0f);
    v.apply_track(trackA);
    CHECK(v.evaluate(Frame{5}) == doctest::Approx(5.0f));

    v.apply_track(trackB);  // clears and replaces
    CHECK(v.evaluate(Frame{5}) == doctest::Approx(150.0f));
}

TEST_CASE("AnimationTrack does not own evaluation — AnimatedValue does") {
    // AnimationTrack is a builder; the AnimatedValue takes ownership
    // of the keyframes via apply_track().
    auto track = AnimationTrack<float>()
        .from(Frame{0}, 42.0f)
        .to(Frame{60}, 42.0f);

    AnimatedValue<float> v(0.0f);
    v.apply_track(track);

    // Modifying the track after baking does not affect the value.
    track.clear();
    CHECK(v.evaluate(Frame{0}) == doctest::Approx(42.0f));
    CHECK(v.is_animated());
}

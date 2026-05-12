#include <doctest/doctest.h>
#include <chronon3d/core/time.hpp>
#include <chronon3d/animation/animated_value.hpp>
#include <chronon3d/math/vec3.hpp>

using namespace chronon3d;

TEST_CASE("Time system") {
    FrameRate fps{30, 1};

    SUBCASE("Conversion") {
        CHECK(fps.to_seconds(30) == doctest::Approx(1.0));
        CHECK(fps.to_seconds(60) == doctest::Approx(2.0));
        CHECK(fps.to_frame(1.0) == 30);
    }

    SUBCASE("TimeRange") {
        TimeRange range{10, 20};
        CHECK(range.duration() == 10);
        CHECK(range.contains(10));
        CHECK(range.contains(15));
        CHECK_FALSE(range.contains(20));
        CHECK(range.normalized(15) == 0.5f);
    }
}

TEST_CASE("AnimatedValue") {
    SUBCASE("Linear interpolation float") {
        AnimatedValue<f32> val(0.0f);
        val.add_keyframe(0, 0.0f);
        val.add_keyframe(100, 10.0f);

        CHECK(val.evaluate(0) == 0.0f);
        CHECK(val.evaluate(50) == 5.0f);
        CHECK(val.evaluate(100) == 10.0f);
        CHECK(val.evaluate(-10) == 0.0f);
        CHECK(val.evaluate(110) == 10.0f);
    }

    SUBCASE("Linear interpolation Vec3") {
        AnimatedValue<Vec3> pos(Vec3(0.0f));
        pos.add_keyframe(0, Vec3(0.0f, 0.0f, 0.0f));
        pos.add_keyframe(100, Vec3(10.0f, 0.0f, 0.0f));

        Vec3 mid = pos.evaluate(50);
        CHECK(mid.x == 5.0f);
        CHECK(mid.y == 0.0f);
        CHECK(mid.z == 0.0f);
    }

    SUBCASE("Easing") {
        AnimatedValue<f32> val(0.0f);
        val.add_keyframe(0, 0.0f, Easing::EaseInQuad);
        val.add_keyframe(100, 10.0f);

        f32 linear_mid = 5.0f;
        f32 eased_mid = val.evaluate(50);
        
        // EaseInQuad means it starts slower, so at 50% time it should be less than 50% value
        CHECK(eased_mid < linear_mid);
        CHECK(eased_mid == 2.5f); // 10 * (0.5 * 0.5)
    }
}

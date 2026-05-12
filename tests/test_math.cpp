#include <doctest/doctest.h>
#include <chronon3d/math/vec2.hpp>
#include <chronon3d/math/vec3.hpp>

using namespace chronon3d;

TEST_CASE("Vec2 operations") {
    Vec2 a{1.0f, 2.0f};
    Vec2 b{3.0f, 4.0f};

    SUBCASE("Addition") {
        Vec2 c = a + b;
        CHECK(c.x == 4.0f);
        CHECK(c.y == 6.0f);
    }

    SUBCASE("Subtraction") {
        Vec2 c = b - a;
        CHECK(c.x == 2.0f);
        CHECK(c.y == 2.0f);
    }

    SUBCASE("Scalar Multiplication") {
        Vec2 c = a * 2.0f;
        CHECK(c.x == 2.0f);
        CHECK(c.y == 4.0f);
    }

    SUBCASE("Dot Product") {
        f32 d = a.dot(b);
        CHECK(d == 11.0f); // 1*3 + 2*4 = 3 + 8 = 11
    }

    SUBCASE("Length") {
        Vec2 v{3.0f, 4.0f};
        CHECK(v.length() == 5.0f);
    }

    SUBCASE("Normalization") {
        Vec2 v{3.0f, 4.0f};
        Vec2 n = v.normalized();
        CHECK(n.length() == doctest::Approx(1.0f));
        CHECK(n.x == 3.0f / 5.0f);
        CHECK(n.y == 4.0f / 5.0f);
    }
}

TEST_CASE("Vec3 operations") {
    Vec3 a{1.0f, 0.0f, 0.0f};
    Vec3 b{0.0f, 1.0f, 0.0f};

    SUBCASE("Cross Product") {
        Vec3 c = a.cross(b);
        CHECK(c.x == 0.0f);
        CHECK(c.y == 0.0f);
        CHECK(c.z == 1.0f);
    }

    SUBCASE("Dot Product") {
        Vec3 v1{1.0f, 2.0f, 3.0f};
        Vec3 v2{4.0f, 5.0f, 6.0f};
        CHECK(v1.dot(v2) == 32.0f); // 1*4 + 2*5 + 3*6 = 4 + 10 + 18 = 32
    }

    SUBCASE("Length") {
        Vec3 v{0.0f, 3.0f, 4.0f};
        CHECK(v.length() == 5.0f);
    }

    SUBCASE("Normalization") {
        Vec3 v{1.0f, 2.0f, 3.0f};
        Vec3 n = v.normalized();
        CHECK(n.length() == doctest::Approx(1.0f));
    }
}

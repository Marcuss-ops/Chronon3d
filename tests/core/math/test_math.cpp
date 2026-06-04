#include <doctest/doctest.h>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/camera.hpp>

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
        f32 d = glm::dot(a, b);
        CHECK(d == 11.0f); // 1*3 + 2*4 = 3 + 8 = 11
    }

    SUBCASE("Length") {
        Vec2 v{3.0f, 4.0f};
        CHECK(glm::length(v) == 5.0f);
    }

    SUBCASE("Normalization") {
        Vec2 v{3.0f, 4.0f};
        Vec2 n = glm::normalize(v);
        CHECK(glm::length(n) == doctest::Approx(1.0f));
        CHECK(n.x == 3.0f / 5.0f);
        CHECK(n.y == 4.0f / 5.0f);
    }
}

TEST_CASE("Vec3 operations") {
    Vec3 a{1.0f, 0.0f, 0.0f};
    Vec3 b{0.0f, 1.0f, 0.0f};

    SUBCASE("Cross Product") {
        Vec3 c = glm::cross(a, b);
        CHECK(c.x == 0.0f);
        CHECK(c.y == 0.0f);
        CHECK(c.z == 1.0f);
    }

    SUBCASE("Dot Product") {
        Vec3 v1{1.0f, 2.0f, 3.0f};
        Vec3 v2{4.0f, 5.0f, 6.0f};
        CHECK(glm::dot(v1, v2) == 32.0f); // 1*4 + 2*5 + 3*6 = 4 + 10 + 18 = 32
    }

    SUBCASE("Length") {
        Vec3 v{0.0f, 3.0f, 4.0f};
        CHECK(glm::length(v) == 5.0f);
    }

    SUBCASE("Normalization") {
        Vec3 v{1.0f, 2.0f, 3.0f};
        Vec3 n = glm::normalize(v);
        CHECK(glm::length(n) == doctest::Approx(1.0f));
    }
}

TEST_CASE("Matrix operations") {
    SUBCASE("Translation") {
        Mat4 m = glm::translate(Mat4(1.0f), Vec3(10, 20, 30));
        Vec4 p_h = m * Vec4(0, 0, 0, 1);
        Vec3 p = Vec3(p_h) / p_h.w;
        CHECK(p.x == 10.0f);
        CHECK(p.y == 20.0f);
        CHECK(p.z == 30.0f);
    }

    SUBCASE("Scale") {
        Mat4 m = glm::scale(Mat4(1.0f), Vec3(2, 3, 4));
        Vec4 p_h = m * Vec4(1, 1, 1, 1);
        Vec3 p = Vec3(p_h) / p_h.w;
        CHECK(p.x == 2.0f);
        CHECK(p.y == 3.0f);
        CHECK(p.z == 4.0f);
    }
}

TEST_CASE("3D Projection") {
    Camera cam;
    cam.transform.position = Vec3(0, 0, 5);
    
    SUBCASE("Perspective check") {
        Mat4 proj = cam.projection_matrix(1.0f);
        Mat4 view = cam.view_matrix();
        Mat4 mvp = proj * view;

        // Point at (0,0,0) in world space
        // With camera at (0,0,5), point is at (0,0,-5) in view space
        Vec4 p_h = mvp * Vec4(0, 0, 0, 1);
        Vec3 p = Vec3(p_h) / p_h.w;
        
        // In NDC, it should be at center (0,0)
        CHECK(p.x == doctest::Approx(0.0f));
        CHECK(p.y == doctest::Approx(0.0f));
    }
}

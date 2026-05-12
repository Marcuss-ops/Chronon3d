#include <doctest/doctest.h>
#include <chronon3d/math/mat4.hpp>
#include <chronon3d/scene/camera.hpp>

using namespace chronon3d;

TEST_CASE("Matrix operations") {
    SUBCASE("Translation") {
        Mat4 m = Mat4::translate(Vec3(10, 20, 30));
        Vec3 p = m.transform_point(Vec3(0, 0, 0));
        CHECK(p.x == 10.0f);
        CHECK(p.y == 20.0f);
        CHECK(p.z == 30.0f);
    }

    SUBCASE("Scale") {
        Mat4 m = Mat4::scale(Vec3(2, 3, 4));
        Vec3 p = m.transform_point(Vec3(1, 1, 1));
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
        Vec3 p = mvp.transform_point(Vec3(0, 0, 0));
        
        // In NDC, it should be at center (0,0)
        CHECK(p.x == doctest::Approx(0.0f));
        CHECK(p.y == doctest::Approx(0.0f));
    }
}

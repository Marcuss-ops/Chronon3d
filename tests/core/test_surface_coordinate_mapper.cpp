#include <chronon3d/runtime/render_surface.hpp>

#include <doctest/doctest.h>

TEST_CASE("SurfaceCoordinateMapper converts canvas rectangles exactly once") {
    const chronon3d::runtime::SurfaceCoordinateMapper mapper{288, 162};

    const auto local = mapper.canvas_to_surface(
        chronon3d::runtime::RectF{288.0f, 162.0f, 1632.0f, 918.0f});
    CHECK(local == chronon3d::runtime::RectF{0.0f, 0.0f, 1344.0f, 756.0f});

    const auto round_trip = mapper.surface_to_canvas(local);
    CHECK(round_trip == chronon3d::runtime::RectF{288.0f, 162.0f, 1632.0f, 918.0f});
}

TEST_CASE("SurfaceCoordinateMapper maps full canvas into tight local basis") {
    const chronon3d::runtime::SurfaceCoordinateMapper mapper{288, 162};
    CHECK(mapper.canvas_to_surface(
        chronon3d::runtime::RectF{0.0f, 0.0f, 1920.0f, 1080.0f}) ==
          chronon3d::runtime::RectF{-288.0f, -162.0f, 1632.0f, 918.0f});
}

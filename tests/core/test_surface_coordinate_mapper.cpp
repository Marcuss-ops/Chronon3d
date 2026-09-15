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

TEST_CASE("source mapping keeps canvas coefficients and local bounds together") {
    chronon3d::runtime::SurfaceAffineTransform transform{};
    chronon3d::runtime::set_canvas_source_mapping(
        transform,
        0.5f, 0.25f, -12.0f,
        -0.125f, 0.75f, 8.0f,
        chronon3d::runtime::RectF{4.0f, 6.0f, 100.0f, 120.0f});

    CHECK(transform.source_x[0] == 0.5f);
    CHECK(transform.source_x[1] == 0.25f);
    CHECK(transform.source_x[2] == -12.0f);
    CHECK(transform.source_x[3] == 4.0f);
    CHECK(transform.source_y[0] == -0.125f);
    CHECK(transform.source_y[1] == 0.75f);
    CHECK(transform.source_y[2] == 8.0f);
    CHECK(transform.source_y[3] == 6.0f);
    CHECK(transform.max_x == 100.0f);
    CHECK(transform.max_y == 120.0f);
}

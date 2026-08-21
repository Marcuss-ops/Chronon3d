#include <doctest/doctest.h>

#include <src/render_graph/nodes/detail/producer_surface_bounds.hpp>

using namespace chronon3d;
using namespace chronon3d::graph::detail;

TEST_CASE("producer surface resolver makes image overlays tight") {
    const raster::BBox watermark{120, 80, 440, 200};
    const auto result = resolve_producer_surface_bounds(
        1920, 1080, ProducerSurfaceKind::Image, &watermark);

    CHECK(result.tight);
    CHECK(result.bounds.x0 == 120);
    CHECK(result.bounds.y0 == 80);
    CHECK(result.width() == 320);
    CHECK(result.height() == 120);
}

TEST_CASE("producer surface resolver keeps video and full backgrounds full canvas") {
    const raster::BBox overlay{120, 80, 440, 200};

    const auto video = resolve_producer_surface_bounds(
        1920, 1080, ProducerSurfaceKind::Video, &overlay);
    CHECK_FALSE(video.tight);
    CHECK(video.width() == 1920);
    CHECK(video.height() == 1080);

    const raster::BBox full{0, 0, 1920, 1080};
    const auto background = resolve_producer_surface_bounds(
        1920, 1080, ProducerSurfaceKind::Background, &full);
    CHECK_FALSE(background.tight);
    CHECK(background.bounds.x0 == 0);
    CHECK(background.bounds.y0 == 0);
    CHECK(background.width() == 1920);
    CHECK(background.height() == 1080);
}

TEST_CASE("producer surface resolver clips image bounds to canvas") {
    const raster::BBox partly_offscreen{-40, 100, 220, 260};
    const auto result = resolve_producer_surface_bounds(
        1920, 1080, ProducerSurfaceKind::Image, &partly_offscreen);

    CHECK(result.tight);
    CHECK(result.bounds.x0 == 0);
    CHECK(result.bounds.y0 == 100);
    CHECK(result.bounds.x1 == 220);
    CHECK(result.bounds.y1 == 260);
}

TEST_CASE("producer surface resolver uses local text dimensions") {
    const auto result = resolve_local_producer_surface(
        1920, 1080, ProducerSurfaceKind::Text, Vec2{-12.5f, -8.0f},
        Vec2{1200.25f, 180.0f});

    CHECK(result.tight);
    CHECK(result.bounds.x0 == 0);
    CHECK(result.bounds.y0 == 0);
    CHECK(result.width() == 1201);
    CHECK(result.height() == 180);
}

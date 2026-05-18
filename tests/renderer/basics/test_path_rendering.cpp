#include <doctest/doctest.h>

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

using namespace chronon3d;

namespace {

std::shared_ptr<Framebuffer> render_path(const PathParams& p) {
    SoftwareRenderer renderer;
    auto scene = SceneBuilder{}
        .path("path", p)
        .build();

    Camera camera;
    auto fb = renderer.render_scene(scene, camera, 100, 100);
    REQUIRE(fb != nullptr);
    return fb;
}

PathParams filled_triangle_params() {
    PathParams p;
    p.fill = Fill::solid_color(Color{1, 0, 0, 1});
    p.stroke.width = 0.0f;
    p.commands = {
        PathCommand::move_to({20, 20}),
        PathCommand::line_to({80, 20}),
        PathCommand::line_to({50, 80}),
        PathCommand::close(),
    };
    return p;
}

PathParams open_line_params(LineCap cap) {
    PathParams p;
    p.stroke.width = 10.0f;
    p.stroke.cap = cap;
    p.commands = {
        PathCommand::move_to({20, 50}),
        PathCommand::line_to({80, 50}),
    };
    return p;
}

} // namespace

TEST_CASE("Path rendering fills closed contours") {
    auto fb = render_path(filled_triangle_params());
    CHECK(fb->get_pixel(50, 40).r > 0.7f);
    CHECK(fb->get_pixel(10, 10).r < 0.1f);
}

TEST_CASE("Path rendering respects butt and round caps") {
    SoftwareRenderer renderer;
    Camera camera;

    auto butt_scene = SceneBuilder{}
        .path("butt", open_line_params(LineCap::Butt))
        .build();
    auto round_scene = SceneBuilder{}
        .path("round", open_line_params(LineCap::Round))
        .build();

    auto butt = renderer.render_scene(butt_scene, camera, 100, 100);
    auto round = renderer.render_scene(round_scene, camera, 100, 100);
    REQUIRE(butt != nullptr);
    REQUIRE(round != nullptr);

    CHECK(butt->get_pixel(15, 50).r < 0.1f);
    CHECK(round->get_pixel(15, 50).r > 0.2f);
}

TEST_CASE("Path rendering produces anti-aliased stroke edges") {
    auto fb = render_path(open_line_params(LineCap::Butt));
    CHECK(fb->get_pixel(50, 50).r > fb->get_pixel(50, 44).r);
    CHECK(fb->get_pixel(50, 44).r > 0.0f);
}

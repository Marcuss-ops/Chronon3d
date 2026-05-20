#include <doctest/doctest.h>

#include <chronon3d/compositions/proofs/lil_dirk_clean.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <tests/helpers/render_regression.hpp>

using namespace chronon3d;
using namespace chronon3d::compositions;
using namespace chronon3d::test;

const Layer* find_layer_by_name(const Scene& scene, const std::string& name) {
    for (const auto& layer : scene.layers()) {
        if (layer.name == name.c_str()) {
            return &layer;
        }
    }
    return nullptr;
}

TEST_CASE("LilDirkClean: projected title winding stays positive at frame 0 and 45") {
    const int width = 1920;
    const int height = 1080;

    FrameContext ctx0{.frame = 0, .duration = 90, .width = width, .height = height};
    FrameContext ctx45{.frame = 45, .duration = 90, .width = width, .height = height};

    const Scene scene0 = build_lil_dirk_clean_scene(ctx0);
    const Scene scene45 = build_lil_dirk_clean_scene(ctx45);

    const Layer* title0 = find_layer_by_name(scene0, "title");
    const Layer* title45 = find_layer_by_name(scene45, "title");
    REQUIRE(title0 != nullptr);
    REQUIRE(title45 != nullptr);

    const auto projected0 = project_layer_2_5d(title0->transform, scene0.camera_2_5d(), static_cast<f32>(width), static_cast<f32>(height));
    const auto projected45 = project_layer_2_5d(title45->transform, scene45.camera_2_5d(), static_cast<f32>(width), static_cast<f32>(height));

    const auto winding0 = projected_quad_signed_area(projected0.projection_matrix, 1.0f, 1.0f);
    const auto winding45 = projected_quad_signed_area(projected45.projection_matrix, 1.0f, 1.0f);

    REQUIRE(winding0.has_value());
    REQUIRE(winding45.has_value());

    CHECK(*winding0 > 0.0f);
    CHECK(*winding45 > 0.0f);
}

TEST_CASE("LilDirkClean: static and sweep frames stay readable and centered") {
    SoftwareRenderer renderer = make_regression_renderer();
    const Composition comp = LilDirkClean();

    const auto sample0 = render_sample(renderer, comp, 0);
    const auto sample45 = render_sample(renderer, comp, 45);

    expect_centered_bright_region(sample0, {comp.width() * 0.5f, comp.height() * 0.5f}, 180.0f, 160.0f);
    expect_centered_bright_region(sample45, {comp.width() * 0.5f, comp.height() * 0.5f}, 220.0f, 160.0f);

    REQUIRE(sample0.centroid.has_value());
    REQUIRE(sample45.centroid.has_value());
    CHECK(std::abs(sample45.centroid->x - sample0.centroid->x) > 2.0f);

    expect_no_projected_winding_flips(renderer);

    save_sample_debug(sample0,  "output/debug/proofs/lil_dirk_clean/frame_000.png");
    save_sample_debug(sample45, "output/debug/proofs/lil_dirk_clean/frame_045.png");
}

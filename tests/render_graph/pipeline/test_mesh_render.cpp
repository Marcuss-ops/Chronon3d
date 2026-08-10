#include <doctest/doctest.h>

#include <chronon3d/assets/mesh_loader.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <tests/helpers/test_utils.hpp>
#include <tests/visual/support/golden_test.hpp>

#include <cmath>
#include <memory>

namespace {

std::shared_ptr<const chronon3d::Mesh> make_triangle(float z) {
    auto mesh = std::make_shared<chronon3d::Mesh>("mesh-test-triangle");
    mesh->add_vertex({{-24.0f, -24.0f, z}});
    mesh->add_vertex({{ 24.0f, -24.0f, z}});
    mesh->add_vertex({{  0.0f,  28.0f, z}});
    mesh->add_index(0);
    mesh->add_index(1);
    mesh->add_index(2);
    return mesh;
}

chronon3d::Mesh make_cube_face(const chronon3d::Vec3& a,
                              const chronon3d::Vec3& b,
                              const chronon3d::Vec3& c,
                              const chronon3d::Vec3& d,
                              const char* name) {
    chronon3d::Mesh mesh{name};
    mesh.add_vertex({a});
    mesh.add_vertex({b});
    mesh.add_vertex({c});
    mesh.add_vertex({d});
    mesh.add_index(0);
    mesh.add_index(1);
    mesh.add_index(2);
    mesh.add_index(0);
    mesh.add_index(2);
    mesh.add_index(3);
    return mesh;
}

chronon3d::Scene make_prepared_mesh_scene() {
    chronon3d::Scene scene;
    auto& resource = *scene.resource();

    chronon3d::Layer layer(&resource);
    layer.name = std::pmr::string{"mesh-layer", &resource};
    layer.kind = chronon3d::LayerKind::Normal;
    layer.asset_manifest.clear();

    chronon3d::RenderNode node(&resource);
    node.name = std::pmr::string{"prepared-mesh", &resource};
    node.color = chronon3d::Color::white();
    node.shape.set_type(chronon3d::ShapeType::Mesh);

    auto prepared = std::make_shared<chronon3d::assets::PreparedMeshSource>();
    prepared->parts.push_back({
        .name = "far",
        .geometry = make_triangle(-1200.0f),
        .material_index = 0,
    });
    prepared->parts.push_back({
        .name = "near",
        .geometry = make_triangle(-1100.0f),
        .material_index = 1,
    });
    prepared->materials.push_back({
        .name = "far-red",
        .base_color_factor = chronon3d::Color{1.0f, 0.0f, 0.0f, 1.0f},
    });
    prepared->materials.push_back({
        .name = "near-blue",
        .base_color_factor = chronon3d::Color{0.0f, 0.0f, 1.0f, 1.0f},
    });
    node.shape.mesh_shape().prepared = std::move(prepared);
    layer.nodes.push_back(std::move(node));
    scene.add_layer(std::move(layer));
    return scene;
}

chronon3d::Scene make_rotated_cube_scene() {
    chronon3d::Scene scene;
    auto& resource = *scene.resource();

    constexpr float h = 60.0f;
    chronon3d::Layer layer(&resource);
    layer.name = std::pmr::string{"rotated-cube-layer", &resource};
    layer.kind = chronon3d::LayerKind::Normal;
    layer.asset_manifest.clear();

    chronon3d::RenderNode node(&resource);
    node.name = std::pmr::string{"rotated-cube", &resource};
    node.color = chronon3d::Color::white();
    node.shape.set_type(chronon3d::ShapeType::Mesh);
    node.world_transform.position = {0.0f, 0.0f, -1500.0f};
    node.world_transform.rotation = glm::quat(glm::radians(
        chronon3d::Vec3{25.0f, -35.0f, 0.0f}));

    auto prepared = std::make_shared<chronon3d::assets::PreparedMeshSource>();
    prepared->materials = {
        {.name = "front", .base_color_factor = chronon3d::Color{0.95f, 0.12f, 0.10f, 1.0f}},
        {.name = "side",  .base_color_factor = chronon3d::Color{0.10f, 0.35f, 0.95f, 1.0f}},
        {.name = "top",   .base_color_factor = chronon3d::Color{0.95f, 0.75f, 0.08f, 1.0f}},
        {.name = "back",  .base_color_factor = chronon3d::Color{0.20f, 0.08f, 0.55f, 1.0f}},
        {.name = "bottom",.base_color_factor = chronon3d::Color{0.08f, 0.55f, 0.25f, 1.0f}},
        {.name = "left",  .base_color_factor = chronon3d::Color{0.85f, 0.18f, 0.55f, 1.0f}},
    };

    auto add_part = [&prepared](chronon3d::Mesh mesh, std::size_t material,
                                const char* name) {
        prepared->parts.push_back({
            .name = name,
            .geometry = std::make_shared<const chronon3d::Mesh>(std::move(mesh)),
            .material_index = material,
        });
    };

    add_part(make_cube_face({-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}, "front"), 0, "front");
    add_part(make_cube_face({h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}, "right"), 1, "right");
    add_part(make_cube_face({-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}, "top"), 2, "top");
    add_part(make_cube_face({h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}, "back"), 3, "back");
    add_part(make_cube_face({-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}, "bottom"), 4, "bottom");
    add_part(make_cube_face({-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}, "left"), 5, "left");

    node.shape.mesh_shape().prepared = std::move(prepared);
    layer.nodes.push_back(std::move(node));
    scene.add_layer(std::move(layer));
    return scene;
}

chronon3d::test::GoldenTestConfig mesh_golden_config() {
    chronon3d::test::GoldenTestConfig cfg;
    cfg.golden_directory = "test_renders/golden/mesh";
    cfg.artifact_directory = "test_renders/artifacts/mesh";
    cfg.mode = chronon3d::test::golden_mode_from_environment();
    cfg.threshold.max_mean_abs_error = 5.0f / 255.0f;
    cfg.threshold.max_abs_error = 40.0f / 255.0f;
    cfg.threshold.max_changed_pixel_ratio = 0.05f;
    cfg.threshold.max_rmse = 6.0f / 255.0f;
    cfg.threshold.min_ssim = 0.92f;
    return cfg;
}

} // namespace

TEST_CASE("RenderGraph Mesh: prepared parts use base color and shared camera depth") {
    constexpr int width = 160;
    constexpr int height = 160;

    auto scene = make_prepared_mesh_scene();
    auto renderer = chronon3d::test::make_renderer();
    chronon3d::cache::NodeCache node_cache;
    chronon3d::Camera camera;

    auto framebuffer = chronon3d::graph::render_scene_via_graph(
        renderer.backend(),
        node_cache,
        scene,
        camera,
        width,
        height,
        0,
        0.0f,
        renderer.render_settings(),
        renderer.composition_registry(),
        renderer.video_decoder(),
        30.0f,
        "mesh-test",
        &renderer);

    REQUIRE(framebuffer != nullptr);
    const auto center = framebuffer->get_pixel(width / 2, height / 2);

    // Both triangles cover the center. The nearer part must win the shared
    // depth test, and its prepared material must supply the blue base color.
    CHECK(center.b > 0.5f);
    CHECK(center.r < 0.1f);
    CHECK(center.a > 0.5f);
}

TEST_CASE("RenderGraph Mesh: rotated cube perspective depth golden") {
    constexpr int width = 256;
    constexpr int height = 256;

    auto scene = make_rotated_cube_scene();
    auto renderer = chronon3d::test::make_renderer();
    chronon3d::cache::NodeCache node_cache;
    chronon3d::Camera camera;
    camera.near_plane = 1.0f;
    camera.far_plane = 800.0f;

    auto framebuffer = chronon3d::graph::render_scene_via_graph(
        renderer.backend(),
        node_cache,
        scene,
        camera,
        width,
        height,
        0,
        0.0f,
        renderer.render_settings(),
        renderer.composition_registry(),
        renderer.video_decoder(),
        30.0f,
        "rotated-cube-golden",
        &renderer);

    REQUIRE(framebuffer != nullptr);

    int colored_pixels = 0;
    int red_pixels = 0;
    int blue_pixels = 0;
    int yellow_pixels = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto pixel = framebuffer->get_pixel(x, y);
            if (pixel.a > 0.5f) ++colored_pixels;
            if (pixel.r > 0.45f && pixel.r > pixel.g * 1.8f && pixel.r > pixel.b * 1.8f) ++red_pixels;
            if (pixel.b > 0.35f && pixel.b > pixel.r * 1.4f && pixel.b > pixel.g * 1.2f) ++blue_pixels;
            if (pixel.r > 0.45f && pixel.g > 0.25f && pixel.b < 0.25f) ++yellow_pixels;
        }
    }
    CHECK(colored_pixels > 1000);
    CHECK(red_pixels > 100);
    CHECK(blue_pixels > 100);
    CHECK(yellow_pixels > 100);

    // The oblique camera-facing cube exposes front (red), right side (blue),
    // and top (yellow) in stable screen regions. These probes complement the
    // golden image with an explicit front/side/top visibility contract.
    CHECK(framebuffer->get_pixel(105, 140).r > 0.6f);
    CHECK(framebuffer->get_pixel(145, 140).b > 0.4f);
    CHECK(framebuffer->get_pixel(120, 105).r > 0.6f);
    CHECK(framebuffer->get_pixel(120, 105).g > 0.4f);

    const auto golden = chronon3d::test::verify_golden(
        *framebuffer, "rotated_cube_perspective_depth", mesh_golden_config());
    REQUIRE_GOLDEN_PASSED(golden);
}

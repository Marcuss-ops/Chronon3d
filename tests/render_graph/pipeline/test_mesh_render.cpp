#include <doctest/doctest.h>

#include <chronon3d/assets/mesh_loader.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <tests/helpers/test_utils.hpp>

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

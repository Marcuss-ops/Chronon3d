#include "src/backends/software/specialized/mesh_renderer.hpp"

#include <doctest/doctest.h>
#include <chronon3d/assets/mesh_loader.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/geometry/mesh.hpp>
#include <chronon3d/rendering/light_context.hpp>

using namespace chronon3d;

TEST_CASE("prepared mesh roughness map changes per-pixel specular shading") {
    Mesh mesh{"roughness-map-triangle"};
    mesh.add_vertex(Vertex{{-1.0f, -1.0f, -3.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}});
    mesh.add_vertex(Vertex{{ 1.0f, -1.0f, -3.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}});
    mesh.add_vertex(Vertex{{ 0.0f,  1.0f, -3.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}});
    mesh.add_index(0); mesh.add_index(1); mesh.add_index(2);

    rendering::LightContext light;
    light.enabled = true;
    light.directional_enabled = true;
    light.direction = {0.5f, 0.0f, 0.8660254f};
    light.diffuse = 0.8f;
    RenderNode::InlineMeshMaterial material;
    material.diffuse = 0.0f;
    material.specular = 1.0f;
    material.shininess = 32.0f;
    std::array<float, 128 * 128> depth{};
    const Mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    assets::MeshImage map;
    map.decoded_width = 2;
    map.decoded_height = 1;
    // Roughness comes from G; metallic comes from B.
    map.decoded_rgba = {255,255,0,255, 255,0,0,255};
    Framebuffer fb(128, 128);
    chronon3d::renderer::render_mesh_filled_lit(
        fb, mesh, Mat4(1.0f), Mat4(1.0f), projection, Color::white(), depth,
        light, {}, material, 0.1f, 100.0f, 0.5f, 0.0f, &map);

    const auto rough_side = fb.get_pixel(48, 70);
    const auto smooth_side = fb.get_pixel(78, 70);
    CHECK(rough_side.r > smooth_side.r + 0.005f);
}

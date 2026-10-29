#include "src/backends/software/specialized/mesh_renderer.hpp"

#include <doctest/doctest.h>
#include <chronon3d/assets/mesh_loader.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/geometry/mesh.hpp>
#include <chronon3d/rendering/light_context.hpp>

#include <array>

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

TEST_CASE("prepared mesh metallic-roughness map samples blue metallic channel") {
    Mesh mesh{"metallic-map-triangle"};
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
    assets::MeshImage map;
    map.decoded_width = 2;
    map.decoded_height = 1;
    // G is constant roughness; B varies from dielectric to metallic.
    map.decoded_rgba = {255,255,0,255, 255,255,255,255};
    std::array<float, 128 * 128> depth{};
    const Mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
    Framebuffer fb(128, 128);
    chronon3d::renderer::render_mesh_filled_lit(
        fb, mesh, Mat4(1.0f), Mat4(1.0f), projection, Color::white(), depth,
        light, {}, material, 0.1f, 100.0f, 0.5f, 1.0f, &map);

    const auto dielectric_side = fb.get_pixel(48, 70);
    const auto metallic_side = fb.get_pixel(78, 70);
    CHECK(metallic_side.r > dielectric_side.r + 0.005f);
}

TEST_CASE("inline emissive mesh material contributes independently of scene lights") {
    Mesh mesh{"emissive-triangle"};
    mesh.add_vertex(Vertex{{-1.f, -1.f, -3.f}, {0.f, 0.f, 1.f}, {0.f, 0.f}});
    mesh.add_vertex(Vertex{{ 1.f, -1.f, -3.f}, {0.f, 0.f, 1.f}, {1.f, 0.f}});
    mesh.add_vertex(Vertex{{ 0.f,  1.f, -3.f}, {0.f, 0.f, 1.f}, {.5f, 1.f}});
    mesh.add_index(0); mesh.add_index(1); mesh.add_index(2);
    rendering::LightContext no_lights;
    RenderNode::InlineMeshMaterial material;
    material.diffuse = 0.f;
    material.specular = 0.f;
    material.rim_intensity = 0.f;
    material.base_color = Color{0.f, 0.f, 0.f, 1.f};
    material.emissive = Color{0.3f, 0.15f, 0.05f, 1.f};
    material.alpha_cutoff = 0.5f;
    std::array<float, 64 * 64> depth{};
    Framebuffer output(64, 64);
    const Mat4 projection = glm::perspective(glm::radians(60.f), 1.f, .1f, 100.f);
    material.base_color = Color{0.f, 0.f, 0.f, 0.4f};
    renderer::render_mesh_filled_lit(output, mesh, Mat4{1.f}, Mat4{1.f}, projection,
        Color::white(), depth, no_lights, {}, material);
    CHECK(output.get_pixel(32, 32).a == doctest::Approx(0.f));
    material.base_color = Color{0.f, 0.f, 0.f, 1.f};
    renderer::render_mesh_filled_lit(output, mesh, Mat4{1.f}, Mat4{1.f}, projection,
        Color::white(), depth, no_lights, {}, material);
    const auto pixel = output.get_pixel(32, 32);
    CHECK(pixel.r == doctest::Approx(0.3f));
    CHECK(pixel.g == doctest::Approx(0.15f));
    CHECK(pixel.b == doctest::Approx(0.05f));
}

TEST_CASE("lit inline mesh multiplies its base-color texture and applies alpha cutoff") {
    Mesh mesh{"lit-textured-triangle"};
    mesh.add_vertex(Vertex{{-1.f, -1.f, -3.f}, {0.f, 0.f, 1.f}, {0.f, 0.f}});
    mesh.add_vertex(Vertex{{ 1.f, -1.f, -3.f}, {0.f, 0.f, 1.f}, {1.f, 0.f}});
    mesh.add_vertex(Vertex{{ 0.f,  1.f, -3.f}, {0.f, 0.f, 1.f}, {.5f, 1.f}});
    mesh.add_index(0); mesh.add_index(1); mesh.add_index(2);
    rendering::LightContext no_lights;
    RenderNode::InlineMeshMaterial material;
    material.diffuse = 0.f;
    material.specular = 0.f;
    material.rim_intensity = 0.f;
    material.base_color = Color{1.f, 1.f, 1.f, 1.f};
    material.emissive = Color{0.f, 0.f, 0.f, 1.f};
    material.alpha_cutoff = 0.5f;
    Framebuffer texture(1, 1);
    texture.set_pixel(0, 0, Color{0.2f, 0.4f, 0.6f, 1.f});
    std::array<float, 64 * 64> depth{};
    Framebuffer output(64, 64);
    const Mat4 projection = glm::perspective(glm::radians(60.f), 1.f, .1f, 100.f);
    renderer::render_mesh_filled_lit(output, mesh, Mat4{1.f}, Mat4{1.f}, projection,
        Color::white(), depth, no_lights, {}, material, .1f, 100.f, -1.f, 0.f, nullptr, &texture);
    const auto pixel = output.get_pixel(32, 32);
    CHECK(pixel.r == doctest::Approx(0.2f));
    CHECK(pixel.g == doctest::Approx(0.4f));
    CHECK(pixel.b == doctest::Approx(0.6f));
    texture.set_pixel(0, 0, Color{0.8f, 0.7f, 0.6f, 0.4f});
    output.clear(Color::transparent());
    depth.fill(0.f);
    renderer::render_mesh_filled_lit(output, mesh, Mat4{1.f}, Mat4{1.f}, projection,
        Color::white(), depth, no_lights, {}, material, .1f, 100.f, -1.f, 0.f, nullptr, &texture);
    CHECK(output.get_pixel(32, 32).a == doctest::Approx(0.f));
}

#include "src/backends/software/specialized/mesh_renderer.hpp"

#include <doctest/doctest.h>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/geometry/mesh.hpp>

using namespace chronon3d;

TEST_CASE("textured inline mesh preserves image alpha through UV rasterization") {
    Mesh mesh{"textured-quad"};
    mesh.add_vertex(Vertex{{-1.f, -1.f, -3.f}, {0.f, 0.f, 1.f}, {0.f, 0.f}});
    mesh.add_vertex(Vertex{{ 1.f, -1.f, -3.f}, {0.f, 0.f, 1.f}, {1.f, 0.f}});
    mesh.add_vertex(Vertex{{ 1.f,  1.f, -3.f}, {0.f, 0.f, 1.f}, {1.f, 1.f}});
    mesh.add_vertex(Vertex{{-1.f,  1.f, -3.f}, {0.f, 0.f, 1.f}, {0.f, 1.f}});
    mesh.add_index(0); mesh.add_index(1); mesh.add_index(2);
    mesh.add_index(0); mesh.add_index(2); mesh.add_index(3);

    Framebuffer texture{2, 2};
    texture.set_pixel(0, 0, Color{0.5f, 0.f, 0.f, 0.5f});
    texture.set_pixel(1, 0, Color{0.5f, 0.f, 0.f, 0.5f});
    texture.set_pixel(0, 1, Color{0.5f, 0.f, 0.f, 0.5f});
    texture.set_pixel(1, 1, Color{0.5f, 0.f, 0.f, 0.5f});
    Framebuffer output{64, 64};
    std::vector<float> depth(64 * 64, 0.f);
    const Mat4 projection = glm::perspective(glm::radians(60.f), 1.f, .1f, 100.f);
    renderer::render_mesh_textured(output, mesh, Mat4{1.f}, Mat4{1.f}, projection,
                                   Color::white(), depth, texture);
    const auto pixel = output.get_pixel(32, 32);
    CHECK(pixel.a == doctest::Approx(0.5f));
    CHECK(pixel.r == doctest::Approx(0.5f));
    CHECK(pixel.g == doctest::Approx(0.f));
}

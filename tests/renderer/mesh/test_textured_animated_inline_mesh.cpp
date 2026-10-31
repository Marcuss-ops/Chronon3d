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

    Framebuffer masked{64, 64};
    std::vector<float> masked_depth(64 * 64, 0.f);
    renderer::render_mesh_textured(masked, mesh, Mat4{1.f}, Mat4{1.f}, projection,
                                   Color::white(), masked_depth, texture,
                                   0.1f, 1000.f, 0.75f);
    const auto discarded = masked.get_pixel(32, 32);
    CHECK(discarded.a == doctest::Approx(0.f));
}

TEST_CASE("textured mesh clips triangles against near and far planes") {
    Framebuffer texture{2, 2};
    texture.clear(Color{0.5f, 0.f, 0.f, 0.5f});
    const Mat4 projection = glm::perspective(glm::radians(60.f), 1.f, 1.f, 4.f);
    const auto render = [&](float first_z, float other_z, std::optional<float> cutoff) {
        Mesh mesh{"clipped-textured-triangle"};
        mesh.add_vertex(Vertex{{-0.7f, -0.7f, first_z}, {0.f, 0.f, 1.f}, {0.f, 0.f}});
        mesh.add_vertex(Vertex{{ 0.7f, -0.7f, other_z}, {0.f, 0.f, 1.f}, {1.f, 0.f}});
        mesh.add_vertex(Vertex{{ 0.f,  0.7f, other_z}, {0.f, 0.f, 1.f}, {0.5f, 1.f}});
        mesh.add_index(0); mesh.add_index(1); mesh.add_index(2);
        Framebuffer output{64, 64};
        std::vector<float> depth(64 * 64, 0.f);
        renderer::render_mesh_textured(output, mesh, Mat4{1.f}, Mat4{1.f}, projection,
                                       Color::white(), depth, texture, 1.f, 4.f, cutoff);
        Framebuffer without_depth{64, 64};
        renderer::render_mesh_textured(without_depth, mesh, Mat4{1.f}, Mat4{1.f}, projection,
                                       Color::white(), {}, texture, 1.f, 4.f, cutoff);
        std::size_t covered = 0;
        for (int y = 0; y < output.height(); ++y) {
            for (int x = 0; x < output.width(); ++x) {
                const auto pixel = output.get_pixel(x, y);
                CHECK(without_depth.get_pixel(x, y).a == doctest::Approx(pixel.a));
                if (pixel.a <= 0.f) continue;
                ++covered;
                CHECK(pixel.a == doctest::Approx(0.5f));
                CHECK(pixel.r == doctest::Approx(0.5f));
                CHECK(depth[static_cast<std::size_t>(y) * output.width() + x] >= 1.f);
                CHECK(depth[static_cast<std::size_t>(y) * output.width() + x] <= 4.f);
            }
        }
        return covered;
    };
    CHECK(render(-0.5f, -2.f, std::nullopt) > 0);
    CHECK(render(-5.f, -2.f, std::nullopt) > 0);
    CHECK(render(-0.5f, -0.5f, std::nullopt) == 0);
    CHECK(render(-5.f, -5.f, std::nullopt) == 0);
    CHECK(render(-0.5f, -2.f, 0.75f) == 0);
}

TEST_CASE("clipped textured mesh does not blend its fan diagonal twice") {
    Mesh mesh{"clipped-fan-diagonal"};
    mesh.add_vertex(Vertex{{-0.5f, 0.f, -0.5f}, {0.f, 0.f, 1.f}, {0.f, 0.f}});
    mesh.add_vertex(Vertex{{1.f, -21.f / 32.f, -2.f}, {0.f, 0.f, 1.f}, {1.f, 0.f}});
    mesh.add_vertex(Vertex{{0.5f, -63.f / 64.f, -2.f}, {0.f, 0.f, 1.f}, {0.5f, 1.f}});
    mesh.add_index(0); mesh.add_index(1); mesh.add_index(2);
    Framebuffer texture{1, 1};
    texture.clear(Color{0.5f, 0.f, 0.f, 0.5f});
    Framebuffer output{64, 64};
    Mat4 projection{0.f};
    projection[0][0] = 1.f;
    projection[1][1] = 1.f;
    projection[2][3] = -1.f;
    renderer::render_mesh_textured(output, mesh, Mat4{1.f}, Mat4{1.f}, projection,
                                   Color::white(), {}, texture, 1.f, 4.f);
    // The clipped polygon's fan diagonal passes through this pixel center.
    CHECK(output.get_pixel(34, 42).a == doctest::Approx(0.5f));
}

TEST_CASE("clipped textured mesh matches explicitly clipped geometry and UVs") {
    Framebuffer texture{2, 2};
    texture.set_pixel(0, 0, Color{1.f, 0.f, 0.f, 1.f});
    texture.set_pixel(1, 0, Color{0.f, 1.f, 0.f, 1.f});
    texture.set_pixel(0, 1, Color{0.f, 0.f, 1.f, 1.f});
    texture.set_pixel(1, 1, Color{1.f, 1.f, 1.f, 1.f});
    const Mat4 projection = glm::perspective(glm::radians(60.f), 1.f, 1.f, 4.f);
    for (const float outside_z : {-0.5f, -5.f}) {
        const Vertex a{{-0.7f, -0.7f, outside_z}, {0.f, 0.f, 1.f}, {0.f, 0.f}};
        const Vertex b{{0.7f, -0.7f, -2.f}, {0.f, 0.f, 1.f}, {1.f, 0.f}};
        const Vertex c{{0.f, 0.7f, -2.f}, {0.f, 0.f, 1.f}, {0.5f, 1.f}};
        const float plane = outside_z > -1.f ? -1.f : -4.f;
        const float t = (plane - outside_z) / (-2.f - outside_z);
        const auto intersection = [&](const Vertex& other) {
            return Vertex{a.position + t * (other.position - a.position), a.normal,
                          a.uv + t * (other.uv - a.uv)};
        };
        Mesh original{"original"};
        for (const auto& v : {a, b, c}) original.add_vertex(v);
        for (const auto i : {0u, 1u, 2u}) original.add_index(i);
        Mesh reference{"explicitly-clipped"};
        for (const auto& v : {intersection(b), b, c, intersection(c)}) reference.add_vertex(v);
        for (const auto i : {0u, 1u, 2u, 0u, 2u, 3u}) reference.add_index(i);
        Framebuffer actual{64, 64}, expected{64, 64};
        std::vector<float> actual_depth(64 * 64, 0.f), expected_depth(64 * 64, 0.f);
        renderer::render_mesh_textured(actual, original, Mat4{1.f}, Mat4{1.f}, projection,
                                       Color::white(), actual_depth, texture, 1.f, 4.f);
        renderer::render_mesh_textured(expected, reference, Mat4{1.f}, Mat4{1.f}, projection,
                                       Color::white(), expected_depth, texture, 1.f, 4.f);
        std::size_t covered = 0;
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                const auto p = actual.get_pixel(x, y);
                const auto q = expected.get_pixel(x, y);
                if (q.a > 0.f) ++covered;
                CHECK(p.r == doctest::Approx(q.r).epsilon(1e-4));
                CHECK(p.g == doctest::Approx(q.g).epsilon(1e-4));
                CHECK(p.b == doctest::Approx(q.b).epsilon(1e-4));
                CHECK(p.a == doctest::Approx(q.a).epsilon(1e-4));
                const auto index = static_cast<std::size_t>(y) * 64 + x;
                CHECK(actual_depth[index] == doctest::Approx(expected_depth[index]).epsilon(1e-4));
            }
        }
        CHECK(covered > 0);
    }
}

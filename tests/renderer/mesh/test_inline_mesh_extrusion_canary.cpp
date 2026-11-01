#include <chronon3d/render_plan/inline_mesh.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <tests/helpers/render_fixtures.hpp>
#include <tests/helpers/test_utils.hpp>
#include <doctest/doctest.h>

using namespace chronon3d;

namespace {

Composition make_extrusion_canary() {
    render_plan::InlineMeshPrimitivePlan primitive;
    primitive.kind = "extrude";
    primitive.depth = 10.0f;
    primitive.profile = {{-14.0f, -14.0f}, {14.0f, -14.0f}, {14.0f, -4.0f},
                         {2.0f, -4.0f}, {2.0f, 14.0f}, {-14.0f, 14.0f}};
    const auto expanded = render_plan::expand_inline_mesh_primitive(primitive);
    InlineMeshParams mesh;
    mesh.name = "concave_extrusion";
    mesh.vertices.reserve(expanded.vertices.size());
    for (const auto& vertex : expanded.vertices) {
        mesh.vertices.emplace_back(
            Vec3{vertex.position[0], vertex.position[1], vertex.position[2]},
            Vec3{vertex.normal[0], vertex.normal[1], vertex.normal[2]},
            Vec2{vertex.uv[0], vertex.uv[1]});
    }
    mesh.indices = expanded.indices;
    return composition({.name = "InlineMeshExtrusionCanary", .width = 64,
                        .height = 64, .duration = 1},
        [mesh = std::move(mesh)](const FrameContext& context) {
            SceneBuilder scene(context);
            scene.set_evaluated_camera(chronon3d::camera().enabled(true).position({0, 0, -80}).zoom(80).rotation({0, 0, 0}).build());
            scene.layer("extrusion", [mesh](LayerBuilder& layer) {
                layer.enable_3d().fill(Color{1, 1, 1, 1}).inline_mesh(mesh);
            });
            return scene.build();
        });
}

} // namespace

TEST_CASE("software 3D mesh crossing a 2.5D plane uses per-pixel depth") {
    InlineMeshParams mesh;
    mesh.name = "crossing_triangle";
    mesh.vertices = {
        Vertex{Vec3{12.f, 12.f, -30.f}, Vec3{0.f, 0.f, -1.f}, Vec2{0.f, 0.f}},
        Vertex{Vec3{52.f, 12.f, 30.f}, Vec3{0.f, 0.f, -1.f}, Vec2{1.f, 0.f}},
        Vertex{Vec3{32.f, 52.f, 0.f}, Vec3{0.f, 0.f, -1.f}, Vec2{0.5f, 1.f}},
    };
    mesh.indices = {0, 1, 2};
    const auto make_scene = [mesh](bool with_plane, bool plane_first,
                                    float mesh_opacity, float plane_opacity) {
        return composition({.name = "CrossingMeshPlane", .width = 64, .height = 64, .duration = 1},
            [mesh, with_plane, plane_first, mesh_opacity, plane_opacity](const FrameContext& context) {
                SceneBuilder scene(context);
                scene.ambient_light(Color::white(), 1.f);
                scene.set_evaluated_camera(camera().enabled(true).position({0, 0, -80}).zoom(80).build());
                const auto add_mesh = [&] {
                    scene.layer("mesh", [mesh, mesh_opacity](LayerBuilder& layer) {
                        layer.enable_3d().opacity(mesh_opacity).inline_mesh(mesh);
                    });
                };
                const auto add_plane = [&] {
                    if (!with_plane) return;
                    scene.layer("plane", [plane_opacity](LayerBuilder& layer) {
                        layer.enable_3d().opacity(plane_opacity);
                        layer.rect("blue", {.size = {64, 64}, .color = Color::blue(), .pos = {0, 0, 0}});
                    });
                };
                if (plane_first) { add_plane(); add_mesh(); }
                else { add_mesh(); add_plane(); }
                auto built = scene.build();
                for (auto& layer : built.layers()) {
                    if (layer.name == "mesh") {
                        for (auto& node : layer.nodes) {
                            if (node.shape.type() == ShapeType::Mesh) node.color = Color::red();
                        }
                    }
                }
                return built;
            });
    };
    const auto mesh_only = test::render_modular(make_scene(false, false, 1.f, 1.f));
    REQUIRE(mesh_only);
    // Both probes are inside the mesh in isolation. With the plane, its
    // nearer left half must remain red; its farther right half must be blue.
    for (int x : {23, 37}) {
        const auto p = mesh_only->get_pixel(x, 32);
        INFO("isolated mesh at " << x << ": " << p.r << ", " << p.g << ", " << p.b << ", " << p.a);
        CHECK(p.r > 0.8f);
    }
    const auto translucent_mesh_only = test::render_modular(make_scene(false, false, 0.5f, 1.f));
    REQUIRE(translucent_mesh_only);
    const auto translucent_pixel = translucent_mesh_only->get_pixel(23, 32);
    CHECK(translucent_pixel.r == doctest::Approx(0.5f));
    CHECK(translucent_pixel.a == doctest::Approx(0.5f));
    CHECK(translucent_pixel.g == doctest::Approx(0.f));
    CHECK(translucent_pixel.b == doctest::Approx(0.f));
    for (bool plane_first : {false, true}) {
        INFO("plane_first=" << plane_first);
        const auto combined = test::render_modular(make_scene(true, plane_first, 1.f, 1.f));
        REQUIRE(combined);
        const auto near = combined->get_pixel(23, 32);
        const auto far = combined->get_pixel(37, 32);
        INFO("opaque near " << near.r << ", " << near.b << "; far " << far.r << ", " << far.b);
        CHECK(near.r > 0.8f);
        CHECK(near.b < 0.1f);
        CHECK(far.b > 0.8f);
        CHECK(far.r < 0.1f);

        const auto transparent_mesh = test::render_modular(make_scene(true, plane_first, 0.5f, 1.f));
        REQUIRE(transparent_mesh);
        const auto mesh_near = transparent_mesh->get_pixel(23, 32);
        const auto mesh_far = transparent_mesh->get_pixel(37, 32);
        INFO("transparent mesh near " << mesh_near.r << ", " << mesh_near.b
             << "; far " << mesh_far.r << ", " << mesh_far.b);
        CHECK(mesh_near.r > 0.35f);
        CHECK(mesh_near.r < 0.65f);
        CHECK(mesh_near.b > 0.35f);
        CHECK(mesh_near.b < 0.65f);
        CHECK(mesh_far.b > 0.8f);
        CHECK(mesh_far.r < 0.1f);

        const auto transparent_plane = test::render_modular(make_scene(true, plane_first, 1.f, 0.5f));
        REQUIRE(transparent_plane);
        const auto plane_near = transparent_plane->get_pixel(23, 32);
        const auto plane_far = transparent_plane->get_pixel(37, 32);
        INFO("transparent plane near " << plane_near.r << ", " << plane_near.b
             << "; far " << plane_far.r << ", " << plane_far.b);
        CHECK(plane_near.r > 0.8f);
        CHECK(plane_near.b < 0.1f);
        CHECK(plane_far.r > 0.35f);
        CHECK(plane_far.r < 0.65f);
        CHECK(plane_far.b > 0.35f);
        CHECK(plane_far.b < 0.65f);
    }
}

TEST_CASE("software untextured mesh stores translucent pixels premultiplied") {
    InlineMeshParams mesh;
    mesh.name = "translucent_triangle";
    mesh.vertices = {
        Vertex{Vec3{12.f, 12.f, 0.f}, Vec3{0.f, 0.f, -1.f}, Vec2{0.f, 0.f}},
        Vertex{Vec3{52.f, 12.f, 0.f}, Vec3{0.f, 0.f, -1.f}, Vec2{1.f, 0.f}},
        Vertex{Vec3{32.f, 52.f, 0.f}, Vec3{0.f, 0.f, -1.f}, Vec2{0.5f, 1.f}},
    };
    mesh.indices = {0, 1, 2};
    const auto scene = composition({.name = "TranslucentMesh", .width = 64, .height = 64, .duration = 1},
        [mesh](const FrameContext& context) {
            SceneBuilder builder(context);
            builder.set_evaluated_camera(camera().enabled(true).position({0, 0, -80}).zoom(80).build());
            builder.layer("mesh", [mesh](LayerBuilder& layer) {
                layer.enable_3d().opacity(0.5f).inline_mesh(mesh);
            });
            auto result = builder.build();
            for (auto& layer : result.layers()) {
                for (auto& node : layer.nodes) {
                    if (node.shape.type() == ShapeType::Mesh) node.color = Color::red();
                }
            }
            return result;
        });
    const auto output = test::render_modular(scene);
    REQUIRE(output);
    const auto pixel = output->get_pixel(32, 32);
    CHECK(pixel.r == doctest::Approx(0.5f));
    CHECK(pixel.g == doctest::Approx(0.f));
    CHECK(pixel.b == doctest::Approx(0.f));
    CHECK(pixel.a == doctest::Approx(0.5f));
}

TEST_CASE("software render canary: concave extruded mesh has stable frame hash") {
    const auto scene = make_extrusion_canary();
    const auto first = test::render_modular(scene);
    const auto second = test::render_modular(scene);
    REQUIRE(first);
    REQUIRE(second);
    const auto frame_hash = test::framebuffer_hash(*first);
    CHECK(frame_hash == test::framebuffer_hash(*second));
    CHECK(frame_hash != 0);
    std::size_t covered_pixels = 0;
    for (int y = 0; y < first->height(); ++y)
        for (int x = 0; x < first->width(); ++x)
            if (first->get_pixel(x, y).a > 0.5f) ++covered_pixels;
    CHECK(covered_pixels > 64);
    INFO("extruded mesh software frame hash: " << frame_hash);
}

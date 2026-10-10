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
            scene.camera_descriptor(chronon3d::camera().enabled(true).position({0, 0, -80}).zoom(80).point_of_interest({0, 0, 0}).build());
            scene.layer("extrusion", [mesh](LayerBuilder& layer) {
                layer.enable_3d().fill(Color{1, 1, 1, 1}).inline_mesh(mesh);
            });
            return scene.build();
        });
}

} // namespace

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

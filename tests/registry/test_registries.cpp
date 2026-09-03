#include <doctest/doctest.h>

#include <chronon3d/registry/shape_registry.hpp>
#include <chronon3d/registry/shape_ids.hpp>
#include <stdexcept>
using namespace chronon3d;

using namespace chronon3d::registry;
namespace shape_ids = chronon3d::registry::shape_ids;

// NOTE: the SourceRegistry / SamplerRegistry test cases were removed with the
// registries themselves (commits d9ce9f99a "remove unused source registry
// header" and e0b69d5c7 "remove unused sampler registry header").  Only the
// live ShapeRegistry surface is covered here.

TEST_CASE("ShapeRegistry exposes built-in shape descriptors") {
    ShapeRegistry registry;

    CHECK(registry.available().size() >= 9);
    CHECK(registry.contains(shape_ids::Rect));
    CHECK(registry.contains(shape_ids::GridBackground));
    CHECK(registry.contains(shape_ids::Path));
    CHECK(registry.get(shape_ids::Path).kind == ShapeKind::Path);
    CHECK(registry.get(shape_ids::Mesh).builtin);
}

TEST_CASE("ShapeRegistry creates built-in shape nodes") {
    ShapeRegistry registry;

    auto node = registry.create_node(
        shape_ids::Rect,
        std::pmr::get_default_resource(),
        "box",
        RectParams{
            .size = {120.0f, 80.0f},
            .color = Color::white(),
            .pos = {10.0f, 20.0f, 30.0f}
        }
    );

    CHECK(node.name == "box");
    CHECK(node.shape.type() == ShapeType::Rect);
    CHECK(node.shape.rect().size.x == doctest::Approx(120.0f));
    CHECK(node.world_transform.position.z == doctest::Approx(30.0f));
    CHECK(node.world_transform.anchor.x == doctest::Approx(60.0f));
    CHECK(node.world_transform.anchor.y == doctest::Approx(40.0f));
}

TEST_CASE("ShapeRegistry creates built-in grid background nodes") {
    ShapeRegistry registry;

    auto node = registry.create_node(
        shape_ids::GridBackground,
        std::pmr::get_default_resource(),
        "grid",
        GridBackgroundParams{
            .size = {1280.0f, 720.0f},
            .offset = {12.0f, 8.0f},
            .bg_color = Color{0.02f, 0.03f, 0.05f, 1.0f},
            .grid_color = Color{0.3f, 0.6f, 1.0f, 0.08f},
            .spacing = 64.0f,
            .minor_thickness = 1.0f,
            .major_thickness = 2.0f,
            .major_every = 4,
            .centered = true
        }
    );

    CHECK(node.shape.type() == ShapeType::GridBackground);
    CHECK(node.shape.grid_background().size.x == doctest::Approx(1280.0f));
    CHECK(node.shape.grid_background().offset.x == doctest::Approx(12.0f));
}

TEST_CASE("ShapeRegistry rejects shapes without factories") {
    ShapeRegistry registry;

    CHECK_THROWS_AS(
        static_cast<void>(registry.create_node(
            shape_ids::Mesh,
            std::pmr::get_default_resource(),
            "mesh",
            RectParams{}
        )),
        std::runtime_error
    );
}

TEST_CASE("ShapeRegistry shape registration and contains check") {
    ShapeRegistry registry;

    CHECK_FALSE(registry.contains("my_custom_poly"));

    ShapeDescriptor desc{
        .id = "my_custom_poly",
        .display_name = "My Custom Polygon",
        .kind = ShapeKind::Path,
        .description = "A polygon shape descriptor",
        .builtin = false
    };

    registry.register_shape(desc);

    CHECK(registry.contains("my_custom_poly"));
    const auto& fetched = registry.get("my_custom_poly");
    CHECK(fetched.display_name == "My Custom Polygon");
    CHECK(fetched.kind == ShapeKind::Path);
    CHECK_FALSE(fetched.builtin);
}

TEST_CASE("ShapeRegistry throws on missing ID") {
    ShapeRegistry shape_reg;

    CHECK_FALSE(shape_reg.contains("missing_id"));
    CHECK_THROWS(static_cast<void>(shape_reg.get("missing_id")));
}

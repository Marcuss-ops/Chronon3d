#include <doctest/doctest.h>

#include <chronon3d/registry.hpp>
#include <stdexcept>

using namespace chronon3d::registry;
using namespace chronon3d;
namespace shape_ids = chronon3d::registry::shape_ids;

TEST_CASE("SourceRegistry exposes built-in source descriptors") {
    SourceRegistry registry;

    CHECK(registry.available().size() == 7);
    CHECK(registry.contains("source.shape"));
    CHECK(registry.contains("source.precomp"));
    CHECK(registry.get("source.video").kind == SourceKind::Video);
    CHECK(registry.get("source.video").temporal);
    CHECK(registry.get("source.adjustment").composable);
}

TEST_CASE("ShapeRegistry exposes built-in shape descriptors") {
    ShapeRegistry registry;

    CHECK(registry.available().size() == 8);
    CHECK(registry.contains(shape_ids::Rect));
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
    CHECK(node.shape.type == ShapeType::Rect);
    CHECK(node.shape.rect.size.x == doctest::Approx(120.0f));
    CHECK(node.world_transform.position.z == doctest::Approx(30.0f));
    CHECK(node.world_transform.anchor.x == doctest::Approx(60.0f));
    CHECK(node.world_transform.anchor.y == doctest::Approx(40.0f));
}

TEST_CASE("ShapeRegistry rejects shapes without factories") {
    ShapeRegistry registry;

    CHECK_THROWS_AS(
        registry.create_node(
            shape_ids::Mesh,
            std::pmr::get_default_resource(),
            "mesh",
            RectParams{}
        ),
        std::runtime_error
    );
}

TEST_CASE("SamplerRegistry exposes built-in sampler descriptors") {
    SamplerRegistry registry;

    CHECK(registry.available().size() == 4);
    CHECK(registry.contains("sampler.nearest"));
    CHECK(registry.contains("sampler.lanczos"));
    CHECK(registry.get("sampler.lanczos").kind == SamplerKind::Lanczos);
}

TEST_CASE("SourceRegistry rejects duplicate ids") {
    SourceRegistry registry;

    CHECK_THROWS_AS(
        registry.register_source(SourceDescriptor{
            .id = "source.shape",
            .display_name = "Duplicate Shape Source",
            .kind = SourceKind::Shape,
        }),
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

TEST_CASE("SamplerRegistry sampler registration and list") {
    SamplerRegistry registry;

    CHECK_FALSE(registry.contains("super_sampler"));

    SamplerDescriptor desc{
        .id = "super_sampler",
        .display_name = "Super Sampler",
        .kind = SamplerKind::Lanczos,
        .description = "Advanced scaling filter",
        .builtin = false
    };

    registry.register_sampler(desc);

    CHECK(registry.contains("super_sampler"));
    const auto& fetched = registry.get("super_sampler");
    CHECK(fetched.display_name == "Super Sampler");
    CHECK(fetched.kind == SamplerKind::Lanczos);

    auto available = registry.available();
    bool found = false;
    for (const auto& name : available) {
        if (name == "super_sampler") {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("SourceRegistry custom source registration") {
    SourceRegistry registry;

    CHECK_FALSE(registry.contains("3d_volumetric"));

    SourceDescriptor desc{
        .id = "3d_volumetric",
        .display_name = "3D Volumetric Source",
        .kind = SourceKind::Layer,
        .description = "Mesh volume layer",
        .builtin = false,
        .temporal = true,
        .composable = true
    };

    registry.register_source(desc);

    CHECK(registry.contains("3d_volumetric"));
    const auto& fetched = registry.get("3d_volumetric");
    CHECK(fetched.temporal == true);
    CHECK(fetched.composable == true);
}

TEST_CASE("Registries throw on missing ID") {
    ShapeRegistry shape_reg;
    SamplerRegistry sampler_reg;
    SourceRegistry source_reg;

    CHECK_FALSE(shape_reg.contains("missing_id"));
    CHECK_FALSE(sampler_reg.contains("missing_id"));
    CHECK_FALSE(source_reg.contains("missing_id"));

    CHECK_THROWS(shape_reg.get("missing_id"));
    CHECK_THROWS(sampler_reg.get("missing_id"));
    CHECK_THROWS(source_reg.get("missing_id"));
}

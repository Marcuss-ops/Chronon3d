#include <doctest/doctest.h>
#include <chronon3d/registry/shape_registry.hpp>
#include <chronon3d/registry/sampler_registry.hpp>
#include <chronon3d/registry/source_registry.hpp>
#include <stdexcept>

using namespace chronon3d::registry;

TEST_CASE("Test 15.1 — ShapeRegistry shape registration and contains check") {
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

TEST_CASE("Test 15.2 — SamplerRegistry sampler registration and list") {
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

TEST_CASE("Test 15.3 — SourceRegistry custom source registration") {
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

TEST_CASE("Test 15.4 — SamplerRegistry contains standard builtins by default") {
    SamplerRegistry registry;

    // Check that built-ins are pre-registered
    CHECK(registry.contains("sampler.nearest"));
    CHECK(registry.contains("sampler.bilinear"));
}

TEST_CASE("Test 15.5 — SourceRegistry contains standard builtins by default") {
    SourceRegistry registry;

    // Standard builtin source kinds exist by default
    CHECK(registry.contains("source.shape"));
    CHECK(registry.contains("source.image"));
    CHECK(registry.contains("source.video"));
}

TEST_CASE("Test 15.6 — Registries throw or fall back gracefully on missing ID") {
    ShapeRegistry shape_reg;
    SamplerRegistry sampler_reg;
    SourceRegistry source_reg;

    CHECK_FALSE(shape_reg.contains("missing_id"));
    CHECK_FALSE(sampler_reg.contains("missing_id"));
    CHECK_FALSE(source_reg.contains("missing_id"));

    // Querying missing ID should not crash, but return a default or dummy struct/status
    CHECK_THROWS(shape_reg.get("missing_id"));
    CHECK_THROWS(sampler_reg.get("missing_id"));
    CHECK_THROWS(source_reg.get("missing_id"));
}

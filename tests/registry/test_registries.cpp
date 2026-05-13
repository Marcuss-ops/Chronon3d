#include <doctest/doctest.h>

#include <chronon3d/registry.hpp>
#include <stdexcept>

using namespace chronon3d::registry;

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
    CHECK(registry.contains("shape.rect"));
    CHECK(registry.contains("shape.path"));
    CHECK(registry.get("shape.path").kind == ShapeKind::Path);
    CHECK(registry.get("shape.mesh").builtin);
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

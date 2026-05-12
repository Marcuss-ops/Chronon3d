#include <doctest/doctest.h>
#include <chronon3d/scene/depth_role.hpp>
#include <chronon3d/scene/layer_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

using namespace chronon3d;

// ---------------------------------------------------------------------------
// DepthRoleResolver
// ---------------------------------------------------------------------------

TEST_CASE("DepthRoleResolver: None resolves to 0") {
    CHECK(DepthRoleResolver::z_for(DepthRole::None) == doctest::Approx(0.0f));
}

TEST_CASE("DepthRoleResolver: Subject resolves to 0") {
    CHECK(DepthRoleResolver::z_for(DepthRole::Subject) == doctest::Approx(0.0f));
}

TEST_CASE("DepthRoleResolver: depth order is correct") {
    // Farther roles have larger (more positive) Z
    CHECK(DepthRoleResolver::z_for(DepthRole::FarBackground) >
          DepthRoleResolver::z_for(DepthRole::Background));
    CHECK(DepthRoleResolver::z_for(DepthRole::Background) >
          DepthRoleResolver::z_for(DepthRole::Midground));
    CHECK(DepthRoleResolver::z_for(DepthRole::Midground) >
          DepthRoleResolver::z_for(DepthRole::Subject));
    CHECK(DepthRoleResolver::z_for(DepthRole::Subject) >
          DepthRoleResolver::z_for(DepthRole::Atmosphere));
    CHECK(DepthRoleResolver::z_for(DepthRole::Atmosphere) >
          DepthRoleResolver::z_for(DepthRole::Foreground));
    CHECK(DepthRoleResolver::z_for(DepthRole::Foreground) >
          DepthRoleResolver::z_for(DepthRole::Overlay));
}

TEST_CASE("DepthRoleResolver: nearer roles have negative Z") {
    CHECK(DepthRoleResolver::z_for(DepthRole::Foreground) < 0.0f);
    CHECK(DepthRoleResolver::z_for(DepthRole::Overlay)    < 0.0f);
    CHECK(DepthRoleResolver::z_for(DepthRole::Atmosphere) < 0.0f);
}

TEST_CASE("DepthRoleResolver: farther roles have positive Z") {
    CHECK(DepthRoleResolver::z_for(DepthRole::Background)    > 0.0f);
    CHECK(DepthRoleResolver::z_for(DepthRole::Midground)     > 0.0f);
    CHECK(DepthRoleResolver::z_for(DepthRole::FarBackground) > 0.0f);
}

// ---------------------------------------------------------------------------
// LayerBuilder integration
// ---------------------------------------------------------------------------

TEST_CASE("LayerBuilder::depth_role sets z from resolver") {
    LayerBuilder builder("test");
    builder.depth_role(DepthRole::Background);
    auto layer = builder.build();
    CHECK(layer.transform.position.z ==
          doctest::Approx(DepthRoleResolver::z_for(DepthRole::Background)));
}

TEST_CASE("LayerBuilder::depth_role + depth_offset applies offset") {
    LayerBuilder builder("test");
    builder.depth_role(DepthRole::Midground).depth_offset(-150.0f);
    auto layer = builder.build();
    CHECK(layer.transform.position.z ==
          doctest::Approx(DepthRoleResolver::z_for(DepthRole::Midground) - 150.0f));
}

TEST_CASE("LayerBuilder::depth_role wins over explicit position.z") {
    // Even if position sets z first, depth_role overrides at build time.
    LayerBuilder builder("test");
    builder.position({100, 200, 9999.0f})   // explicit z
           .depth_role(DepthRole::Subject);  // should override z
    auto layer = builder.build();
    CHECK(layer.transform.position.z == doctest::Approx(0.0f)); // Subject = 0
    // x and y must be preserved
    CHECK(layer.transform.position.x == doctest::Approx(100.0f));
    CHECK(layer.transform.position.y == doctest::Approx(200.0f));
}

TEST_CASE("LayerBuilder::depth_role None does not override position.z") {
    LayerBuilder builder("test");
    builder.position({0, 0, 777.0f});
    auto layer = builder.build();
    CHECK(layer.transform.position.z == doctest::Approx(777.0f));
}

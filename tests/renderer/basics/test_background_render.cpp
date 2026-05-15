#ifdef CHRONON_BUILD_TESTS
#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition_registration.hpp>
#include <chronon3d/compositions/background_grid.hpp>

namespace chronon3d {

TEST_CASE("BackgroundGrid is registered and valid") {
    CompositionRegistry registry;
    CHECK(registry.contains("BackgroundGrid"));
    
    auto comp = registry.create("BackgroundGrid");
    CHECK(comp.width() == 1280);
    CHECK(comp.height() == 720);
}

} // namespace chronon3d
#endif

#include <doctest/doctest.h>
#include <chronon3d/core/composition_registration.hpp>

using namespace chronon3d;

static Composition TestComp() {
    CompositionSpec spec{.name = "TestComp"};
    return Composition{spec, [](const FrameContext&) {
        return Scene{};
    }};
}

CHRONON_REGISTER_COMPOSITION("TestAutoReg", TestComp)

TEST_CASE("Composition Registry & Registration") {
    
    SUBCASE("Manual registration and duplication check") {
        CompositionRegistry registry;
        registry.add("A", TestComp);
        CHECK(registry.contains("A") == true);
        
        // Duplicate registration should throw
        CHECK_THROWS_AS(registry.add("A", TestComp), std::runtime_error);
    }

    SUBCASE("Deterministic available() list") {
        CompositionRegistry registry;
        registry.add("C", TestComp);
        registry.add("A", TestComp);
        registry.add("B", TestComp);
        
        auto ids = registry.available();
        REQUIRE(ids.size() >= 3);
        CHECK(ids[0] == "A");
        CHECK(ids[1] == "B");
        CHECK(ids[2] == "C");
    }

    SUBCASE("Auto-registration integration") {
        CompositionRegistry registry;
        register_all_compositions(registry);
        CHECK(registry.contains("TestAutoReg") == true);
        
        auto comp = registry.create("TestAutoReg");
        CHECK(comp.name() == "TestComp");
    }
}

#include <doctest/doctest.h>
#include <chronon3d/core/composition_registration.hpp>

using namespace chronon3d;

Composition TestComp() {
    CompositionSpec spec{.name = "TestComp"};
    return Composition{spec, [](const FrameContext&) {
        return Scene{};
    }};
}

CHRONON_REGISTER_COMPOSITION("TestAutoReg", TestComp)

TEST_CASE("Composition Auto-registration") {
    CompositionRegistry registry;
    register_all_compositions(registry);

    SUBCASE("Registered composition is present") {
        CHECK(registry.contains("TestAutoReg") == true);
    }

    SUBCASE("Registry returns the correct ID list") {
        auto ids = registry.available();
        bool found = false;
        for (const auto& id : ids) {
            if (id == "TestAutoReg") found = true;
        }
        CHECK(found == true);
    }

    SUBCASE("Create from registered factory") {
        auto comp = registry.create("TestAutoReg");
        CHECK(comp.name() == "TestComp");
    }
}

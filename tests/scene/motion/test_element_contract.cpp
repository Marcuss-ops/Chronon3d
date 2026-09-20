#include <doctest/doctest.h>
#include <chronon/abi.h>
#include <chronon3d/motion/element_contract.hpp>

#include <cmath>
#include <type_traits>
#include <limits>

TEST_CASE("ABI-034 element state is POD and self describing") {
    static_assert(std::is_trivially_copyable_v<chronon_element_state>);
    static_assert(std::is_standard_layout_v<chronon_element_state>);
    chronon_element_state state{};
    state.struct_size = sizeof(state);
    state.layer = 4;
    state.element = 9;
    state.visible = 1;
    state.scale[0] = state.scale[1] = state.scale[2] = 1.f;
    state.opacity = 0.5f;
    state.blur = 2.f;
    CHECK(chronon_contract_version() >= 1u);
    CHECK(state.struct_size == sizeof(chronon_element_state));
}

TEST_CASE("ABI-034 element states apply by layer and stable element id") {
    chronon3d::compiler::FrameParameterBlock block;
    block.layers.resize(1);
    block.layers[0].id = 4;
    block.layers[0].elements.push_back({});
    block.layers[0].elements[0].id = 9;

    chronon_element_state state{};
    state.struct_size = sizeof(state);
    state.layer = 4;
    state.element = 9;
    state.visible = 0;
    state.position[0] = 3.f;
    state.scale[0] = state.scale[1] = state.scale[2] = 2.f;
    state.opacity = 0.25f;
    state.blur = 1.5f;

    chronon3d::motion::ElementApplyReport report{};
    CHECK(chronon3d::motion::apply_element_states(&state, 1, block, &report));
    CHECK(report.applied == 1);
    CHECK(block.layers[0].elements[0].position.x == doctest::Approx(3.f));
    CHECK(block.layers[0].elements[0].opacity == doctest::Approx(0.25f));
    CHECK_FALSE(block.layers[0].elements[0].active);
}

TEST_CASE("ABI-034 element states reject duplicate and non-finite records") {
    chronon_element_state states[2]{};
    for (auto& state : states) {
        state.struct_size = sizeof(state);
        state.layer = 4;
        state.element = 9;
        state.scale[0] = state.scale[1] = state.scale[2] = 1.f;
        state.opacity = 1.f;
    }
    chronon3d::motion::ElementApplyReport report{};
    CHECK_FALSE(chronon3d::motion::validate_element_states(states, 2, &report));
    CHECK(report.rejected == 1);
    states[0].position[1] = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(chronon3d::motion::validate_element_states(states, 1, &report));
    CHECK(report.rejected == 1);
}

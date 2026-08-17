# -- C ABI / render-plan schema validator tests --
#
# Standalone test bundle for the chronon.render-plan JSON Schema validator.
# Lives under tests/c_abi/ (new folder); uses the chronon3d_add_test_suite
# macro at the UNIT tier.  Default UNIT link contract is chronon3d_pipeline;
# this suite uses LINK_TARGETS override to pull only chronon3d_render_plan
# + nlohmann_json::nlohmann_json + doctest (the validator is pure C++ with no SDK state).
#
# Linked into ctest by registration in tests/manifests/test_definitions.cmake.

set(_chronon3d_c_abi_links
    chronon3d_render_plan
    chronon3d_render_plan_compiler
    nlohmann_json::nlohmann_json)
set(_chronon3d_c_abi_sources
    c_abi/test_render_plan_decoder.cpp
    c_abi/test_render_plan_validator.cpp
    c_abi/test_prepared_render_plan.cpp
    c_abi/test_animation_intent.cpp)
if(TARGET chronon3d_c)
    list(APPEND _chronon3d_c_abi_links chronon3d_c)
    list(APPEND _chronon3d_c_abi_sources c_abi/test_c_api_v2.cpp)
endif()

chronon3d_add_test_suite(
    NAME chronon3d_c_abi_tests
    TIER UNIT
    LINK_TARGETS ${_chronon3d_c_abi_links}
    SOURCES ${_chronon3d_c_abi_sources}
    )

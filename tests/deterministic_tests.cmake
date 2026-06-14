# ── Determinism Tests ──
# Tests for pixel-perfect determinism, semantic comparison, and cache state effects.

add_executable(chronon3d_deterministic_tests
    ${TEST_MAIN}
    deterministic/test_deterministic.cpp
    deterministic/test_determinism_harness.cpp
)

target_link_libraries(chronon3d_deterministic_tests
    PRIVATE
        chronon3d_backend_software
        chronon3d_scene
        doctest::doctest
)

target_compile_definitions(chronon3d_deterministic_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(chronon3d_deterministic_tests PRIVATE ${CMAKE_SOURCE_DIR})
set_target_properties(chronon3d_deterministic_tests PROPERTIES UNITY_BUILD OFF)
chronon3d_enable_test_pch(chronon3d_deterministic_tests)
add_test(NAME chronon3d_deterministic_tests COMMAND chronon3d_deterministic_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

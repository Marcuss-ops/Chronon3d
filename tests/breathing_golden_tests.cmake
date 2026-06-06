# ── Breathing Golden Regression Tests ──
# End-to-end golden test for MinimalistImageTrackingBreathing at frame 50.
# Kept separate from chronon3d_renderer_tests to avoid linker OOM.

add_executable(chronon3d_breathing_golden_tests
    ${TEST_MAIN}
    golden/test_breathing_golden.cpp
)

target_link_libraries(chronon3d_breathing_golden_tests
    PRIVATE
        chronon3d_backend_software
        chronon3d_scene
        doctest::doctest
)

target_compile_definitions(chronon3d_breathing_golden_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(chronon3d_breathing_golden_tests PRIVATE ${CMAKE_SOURCE_DIR})
set_target_properties(chronon3d_breathing_golden_tests PROPERTIES UNITY_BUILD OFF)
chronon3d_enable_test_pch(chronon3d_breathing_golden_tests)
add_test(NAME chronon3d_breathing_golden_tests COMMAND chronon3d_breathing_golden_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

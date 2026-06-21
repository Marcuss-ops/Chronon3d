# ── Gradient Visual Golden Tests ──────────────────────────────────────────
# Golden image regression tests for gradient fills and the GradientDefinition
# sampler.  Uses the shared chronon3d_visual_test_support framework.

add_executable(chronon3d_gradient_visual_tests
    ${TEST_MAIN}
    visual/gradient_visual_tests.cpp
)

target_link_libraries(chronon3d_gradient_visual_tests
    PRIVATE
        chronon3d_sdk
        chronon3d_visual_test_support
        chronon3d_backend_software
        chronon3d_backend_image
        chronon3d_scene
        doctest::doctest
)

target_compile_definitions(chronon3d_gradient_visual_tests
    PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_include_directories(chronon3d_gradient_visual_tests
    PRIVATE ${CMAKE_SOURCE_DIR}
)

set_target_properties(chronon3d_gradient_visual_tests PROPERTIES UNITY_BUILD OFF)
chronon3d_enable_test_pch(chronon3d_gradient_visual_tests)

add_test(NAME chronon3d_gradient_visual_tests
    COMMAND chronon3d_gradient_visual_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

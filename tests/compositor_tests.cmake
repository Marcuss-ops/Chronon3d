# ── Compositor Regression Tests ──
# Small, fast unit tests for the software compositor.
# Kept separate from chronon3d_renderer_tests to avoid linker OOM.

add_executable(chronon3d_compositor_tests
    ${TEST_MAIN}
    render_graph/pipeline/test_composite_origin_regression.cpp
    compositor/test_blend_reference.cpp
    compositor/test_blend_simd_equivalence.cpp
    compositor/test_track_matte.cpp
)

target_link_libraries(chronon3d_compositor_tests
    PRIVATE
        chronon3d_sdk
        chronon3d_backend_software
        chronon3d_scene
        doctest::doctest
)

target_compile_definitions(chronon3d_compositor_tests PRIVATE CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(chronon3d_compositor_tests PRIVATE ${CMAKE_SOURCE_DIR})
set_target_properties(chronon3d_compositor_tests PROPERTIES UNITY_BUILD OFF)
chronon3d_enable_test_pch(chronon3d_compositor_tests)
add_test(NAME chronon3d_compositor_tests COMMAND chronon3d_compositor_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

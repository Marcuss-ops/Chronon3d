# ── Graphics Tests (gradient sampler, shape styles, path modifiers) ──

add_executable(chronon3d_graphics_tests
    ${TEST_MAIN}
    graphics/test_gradient_sampler.cpp
    graphics/test_fill_style.cpp
)
target_link_libraries(chronon3d_graphics_tests
    PRIVATE
        chronon3d
        doctest::doctest
)
target_include_directories(chronon3d_graphics_tests PRIVATE ${CMAKE_SOURCE_DIR})
chronon3d_enable_test_pch(chronon3d_graphics_tests)
add_test(NAME chronon3d_graphics_tests COMMAND chronon3d_graphics_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

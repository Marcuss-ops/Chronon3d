add_executable(chronon3d_graphics_tests
    ${TEST_MAIN}
    graphics/test_gradient_sampler.cpp
    graphics/test_fill_style.cpp
    graphics/test_fill_style_integration.cpp
)
target_include_directories(chronon3d_graphics_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_graphics_tests
    PRIVATE
        chronon3d_pipeline
        chronon3d_scene
        chronon3d_backend_software
        chronon3d
        doctest::doctest
)

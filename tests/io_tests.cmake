# ── IO Tests ──

add_executable(chronon3d_io_tests
    ${TEST_MAIN}
    io/test_image_writer.cpp
    io/test_png_validity.cpp
)
if(CHRONON3D_ENABLE_EXR)
    target_sources(chronon3d_io_tests PRIVATE io/test_exr_writer.cpp)
endif()
target_link_libraries(chronon3d_io_tests PRIVATE chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline chronon3d_backend_image doctest::doctest)
target_include_directories(chronon3d_io_tests PRIVATE ${CMAKE_SOURCE_DIR})
chronon3d_enable_test_pch(chronon3d_io_tests)
add_test(NAME chronon3d_io_tests COMMAND chronon3d_io_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

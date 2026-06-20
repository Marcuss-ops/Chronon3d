# ==============================================================================
# tests/expressions/CMakeLists.txt — chronon3d_expressions_v2_tests
# ==============================================================================
#
# Path B test executable. Keeps the v2 tests isolated from scene_tests so a
# future breakage in either suite doesn't mask the other.

add_executable(chronon3d_expressions_v2_tests
    test_expressions_v2.cpp
)
target_link_libraries(chronon3d_expressions_v2_tests
    PRIVATE
        chronon3d_expressions_v2
        doctest::doctest
)
target_include_directories(chronon3d_expressions_v2_tests
    PRIVATE ${CMAKE_SOURCE_DIR}
)
chronon3d_enable_test_pch(chronon3d_expressions_v2_tests)
add_test(NAME chronon3d_expressions_v2_tests
         COMMAND chronon3d_expressions_v2_tests
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

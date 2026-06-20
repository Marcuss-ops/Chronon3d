# ==============================================================================
# tests/expressions/CMakeLists.txt — chronon3d_expressions_v2_tests
# ==============================================================================
#
# Path B test executable. Keeps the v2 tests isolated from scene_tests so a
# future breakage in either suite doesn't mask the other.

# include() does NOT change CMAKE_CURRENT_LIST_DIR (only add_subdirectory does),
# so bare source names are resolved against the parent tests/CMakeLists.txt
# scope.  We must qualify every source file with CMAKE_CURRENT_LIST_DIR.
# Shared doctest main + Path B tests.  TEST_MAIN is set in tests/CMakeLists.txt
# (`set(TEST_MAIN test_main.cpp)`); include() does not change scope, so it
# resolves correctly thanks to the parent setting it before include().
add_executable(chronon3d_expressions_v2_tests
    ${TEST_MAIN}
    ${CMAKE_CURRENT_LIST_DIR}/test_expressions_v2.cpp
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

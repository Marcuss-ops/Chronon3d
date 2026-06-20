# ==============================================================================
# experimental/expressions/tests/CMakeLists.txt — experimental test executable
# ==============================================================================
#
# Gated on CHRONON3D_BUILD_TESTS (parent tests/CMakeLists.txt flag) and
# CHRONON3D_BUILD_EXPERIMENTAL (root CMakeLists.txt flag). Only included when
# both are ON, so a default build does not waste cycles on the quarantine.
#
# Note on `${TEST_MAIN}` scoping: the parent `tests/CMakeLists.txt` defines
# `TEST_MAIN` as a directory-scoped `set()` without `PARENT_SCOPE`. When this
# subtree is added from the root via `add_subdirectory(experimental/expressions)`,
# the experimental subdir runs BEFORE tests/, so `${TEST_MAIN}` is unset in
# this scope. We sidestep the issue by inlining the absolute path to
# test_main.cpp — this experimental subtree only ever consumes the one file.

add_executable(chronon3d_expressions_v2_tests
    ${CMAKE_SOURCE_DIR}/tests/test_main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_expressions_v2.cpp
    ${CMAKE_CURRENT_LIST_DIR}/test_v2_headers.cpp
    # Gate 2 (determinism): section A single-frame drift, B multi-frame drift,
    # C Vec3 arithmetic completeness, D ternary right-associativity regression,
    # E no-static-state-leak.  Wired into the same executable as the rest of
    # the v2 quarantine so it benefits from the same CHRONON3D_BUILD_EXPERIMENTAL
    # gate.
    ${CMAKE_CURRENT_LIST_DIR}/test_expressions_v2_determinism.cpp
)
target_link_libraries(chronon3d_expressions_v2_tests
    PRIVATE
        chronon3d_expressions_v2
        doctest::doctest
)
target_include_directories(chronon3d_expressions_v2_tests
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../include
)
chronon3d_enable_test_pch(chronon3d_expressions_v2_tests)
add_test(NAME chronon3d_expressions_v2_tests
         COMMAND chronon3d_expressions_v2_tests
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

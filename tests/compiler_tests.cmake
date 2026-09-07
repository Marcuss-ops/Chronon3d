if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_compiler_boundary_tests
    TIER UNIT
    SOURCES compiler/test_compiler_boundary.cpp
)

add_test(
    NAME chronon3d_compiler_boundary_gate
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/ci/check_compiler_boundary.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME chronon3d_runtime_ownership_gate
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/ci/check_runtime_ownership.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

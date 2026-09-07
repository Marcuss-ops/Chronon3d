if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_compiler_boundary_tests
    TIER UNIT
    SOURCES compiler/test_compiler_boundary.cpp
)

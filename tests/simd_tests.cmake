# SIMD test suites share one domain manifest.
if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_simd_parity_blend_tests
    TIER UNIT
    SOURCES simd/test_simd_parity_blend.cpp
)

chronon3d_add_test_suite(
    NAME chronon3d_cpu_isa_tests
    TIER UNIT
    SOURCES simd/test_cpu_isa.cpp
)

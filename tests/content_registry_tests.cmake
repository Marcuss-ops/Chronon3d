if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_content_registry_tests
    TIER UNIT
    SOURCES registry/test_content_registry.cpp
)

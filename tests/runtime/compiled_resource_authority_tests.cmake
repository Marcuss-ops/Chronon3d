chronon3d_add_test_suite(
    NAME chronon3d_compiled_resource_authority_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_pipeline
    SOURCES
        runtime/test_compiled_resource_authority.cpp
)

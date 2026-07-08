chronon3d_add_test_suite(
    NAME chronon3d_graphics_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline chronon3d_scene chronon3d_backend_software chronon3d
    SOURCES graphics/test_gradient_sampler.cpp
            graphics/test_fill_style.cpp
            graphics/test_fill_style_integration.cpp
)

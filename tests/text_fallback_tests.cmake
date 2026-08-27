# FontFallbackResolver unit/integration tests.
if(NOT (CHRONON3D_BUILD_TESTS AND CHRONON3D_ENABLE_TEXT))
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_text_fallback_tests
    TIER INTEGRATION
    SOURCES text/test_font_fallback_resolver.cpp
            render_graph/nodes/test_text_upload_scratch_reuse.cpp
)

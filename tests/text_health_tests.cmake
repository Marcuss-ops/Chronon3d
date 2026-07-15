# Focused end-to-end health check for the canonical text pipeline.
if(NOT (CHRONON3D_BUILD_TESTS AND CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D))
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_text_health_tests
    TIER INTEGRATION
    SOURCES text/test_text_health.cpp
    LABELS text-health
)

# One-command local health check after the build tree has been configured:
#   cmake --build <build-dir> --target chronon3d_text_health
add_custom_target(chronon3d_text_health
    COMMAND ${CMAKE_CTEST_COMMAND}
        --output-on-failure
        -R "^chronon3d_text_health_tests$"
    DEPENDS chronon3d_text_health_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    USES_TERMINAL
)

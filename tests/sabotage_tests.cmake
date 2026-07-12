if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_sabotage_tests
    TIER INTEGRATION
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/sabotage/test_sabotage_font_missing_or_corrupt.cpp
    LABELS
        "sabotage;integration"
)

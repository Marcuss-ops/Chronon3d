# Focused SVG importer regression suite.
if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_svg_path_loader_tests
    TIER INTEGRATION
    LINK_TARGETS
        chronon3d_pipeline
        chronon3d_backend_software
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/test_svg_path_loader.cpp
)

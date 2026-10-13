# Focused SVG importer regression suite.
if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()
if(NOT CHRONON3D_ENABLE_SVG)
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

if(TARGET chronon3d_cli AND CHRONON3D_ENABLE_VIDEO)
    add_test(
        NAME chronon3d_svg_cli_frame_gate
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/scripts/ci/check_svg_cli_frame_gate.py"
    )
    set_tests_properties(chronon3d_svg_cli_frame_gate PROPERTIES
        ENVIRONMENT "CHRONON_CLI=$<TARGET_FILE:chronon3d_cli>"
        LABELS "svg;render-plan;cli;gate"
    )
endif()

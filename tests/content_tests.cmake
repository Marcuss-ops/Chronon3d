# ── Content Module Tests (registration contracts + composition smoke) ──
if(NOT CHRONON3D_BUILD_CONTENT)
    return()
endif()

# These legacy shaping tests still pass a literal source-relative font path and
# the canonical test helper runs suites with WORKING_DIRECTORY=CMAKE_SOURCE_DIR.
# The explicit bootstrap therefore installs into the disposable checkout's
# assets/fonts directory. Normal local builds remain network-free because the
# option defaults OFF.
option(CHRONON3D_BOOTSTRAP_TEST_FONTS
    "Fetch checksum-pinned Poppins fixtures into source-relative assets/fonts"
    OFF)

chronon3d_add_test_suite(
    NAME chronon3d_content_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline chronon3d_scene chronon3d_backend_software
    SOURCES content/test_content_module_contract.cpp
            content/test_content_composition_smoke.cpp
            content/test_shaped_glyph_line.cpp
            content/test_shaped_glyph_line_cluster_golden.cpp
            content/test_shaped_glyph_line_cluster_benchmark.cpp
            certification/test_cert_text_bbox.cpp
            certification/test_cert_text_invariants.cpp
)

if(CHRONON3D_BOOTSTRAP_TEST_FONTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_custom_target(chronon3d_bootstrap_test_fonts
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tools/bootstrap_test_fonts.py
                --output-root ${CMAKE_SOURCE_DIR}
        BYPRODUCTS
            ${CMAKE_SOURCE_DIR}/assets/fonts/Poppins-Regular.ttf
            ${CMAKE_SOURCE_DIR}/assets/fonts/Poppins-OFL.txt
        COMMENT "[Chronon3D] Fetching checksum-pinned source-relative Poppins fixtures"
        VERBATIM
    )
    add_dependencies(chronon3d_content_tests chronon3d_bootstrap_test_fonts)
endif()

if(CHRONON3D_BUILD_CONTENT)
    target_sources(chronon3d_content_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core/timeline/test_sequence_v2_compositions.cpp
    )
    target_link_libraries(chronon3d_content_tests PRIVATE chronon3d_content)
    target_compile_definitions(chronon3d_content_tests PRIVATE CHRONON3D_HAS_CONTENT_MINIMALIST CHRONON3D_HAS_CONTENT_2D5)
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/core/timeline/test_sequence_v2_compositions.cpp
        PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
    )
endif()

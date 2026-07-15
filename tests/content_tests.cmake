# ── Content Module Tests (registration contracts + composition smoke) ──
# Per-area early-return gate (TICKET-CMAKE-TEST-MANIFEST-UNIFICATION).
# Compiled only when CHRONON3D_BUILD_CONTENT is on (matches the
# pre-refactor orchestrator's `if(CHRONON3D_BUILD_CONTENT)` block).
if(NOT CHRONON3D_BUILD_CONTENT)
    return()
endif()

# Test font fixtures are downloaded only when explicitly enabled.  The helper
# fetches pinned GitHub blob objects and verifies their Git object checksums;
# normal offline/local configurations remain network-free.
option(CHRONON3D_BOOTSTRAP_TEST_FONTS
    "Fetch checksum-pinned Poppins test fixtures into the build asset root"
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
                --output-root ${CMAKE_BINARY_DIR}
        BYPRODUCTS
            ${CMAKE_BINARY_DIR}/assets/fonts/Poppins-Regular.ttf
            ${CMAKE_BINARY_DIR}/assets/fonts/Poppins-OFL.txt
        COMMENT "[Chronon3D] Fetching checksum-pinned Poppins test fixtures"
        VERBATIM
    )
    add_dependencies(chronon3d_content_tests chronon3d_bootstrap_test_fonts)
endif()

# WHOLE_ARCHIVE removed — content uses explicit ExtensionRegistry registration
if(CHRONON3D_BUILD_CONTENT)
    target_sources(chronon3d_content_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core/timeline/test_sequence_v2_compositions.cpp
    )
    target_link_libraries(chronon3d_content_tests PRIVATE chronon3d_content)
    target_compile_definitions(chronon3d_content_tests PRIVATE CHRONON3D_HAS_CONTENT_MINIMALIST CHRONON3D_HAS_CONTENT_2D5)
    # Unity-build exclusion: forward-declared content symbols + doctest
    # main() would collide in unity batches.
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/core/timeline/test_sequence_v2_compositions.cpp
        PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
    )
endif()

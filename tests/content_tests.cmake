# ── Content Module Tests (registration contracts + composition smoke) ──
# RETIRED with the content externalization (6e6905116): every TU in this
# suite includes <content/...> headers that left the core repo, and the
# chronon3d_content link target no longer exists.  The suite only built
# with CHRONON3D_BUILD_CONTENT=ON, which requires the external content pack.
if(NOT CHRONON3D_BUILD_CONTENT)
    return()
endif()

chronon3d_add_test_suite(
    NAME chronon3d_content_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline chronon3d_scene chronon3d_backend_software
    SOURCES content/test_content_module_contract.cpp
            content/test_content_composition_smoke.cpp
            content/test_2d5_projected_certification.cpp
            content/test_shaped_glyph_line.cpp
            content/test_shaped_glyph_line_cluster_golden.cpp
            content/test_shaped_glyph_line_cluster_benchmark.cpp
)

if(CHRONON3D_BUILD_CONTENT)
    target_sources(chronon3d_content_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core/timeline/test_sequence_v2_compositions.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/content/test_light_transition_sequential_cache.cpp
    )
    target_link_libraries(chronon3d_content_tests PRIVATE chronon3d_content)
    target_compile_definitions(chronon3d_content_tests PRIVATE CHRONON3D_HAS_CONTENT_MINIMALIST CHRONON3D_HAS_CONTENT_2D5)
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/core/timeline/test_sequence_v2_compositions.cpp
        PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
    )
endif()

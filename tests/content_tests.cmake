# ── Content Module Tests (registration contracts + composition smoke) ──

chronon3d_add_test_suite(
    NAME chronon3d_content_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline chronon3d_scene chronon3d_backend_software
    SOURCES content/test_content_module_contract.cpp
            content/test_content_composition_smoke.cpp
            certification/test_cert_text_bbox.cpp
            certification/test_cert_text_invariants.cpp
)
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

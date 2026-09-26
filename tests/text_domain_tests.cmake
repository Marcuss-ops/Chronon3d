# Consolidated text-domain test registration.
# Preserves each former manifest's feature gates, target names, labels,
# sources and aggregate membership while keeping one CMake authority.

if(CHRONON3D_BUILD_TESTS AND CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D)
    chronon3d_add_test_suite(
        NAME chronon3d_text_health_tests
        TIER INTEGRATION
        SOURCES text/test_text_health.cpp
        LABELS text-health
    )
    add_custom_target(chronon3d_text_health
        COMMAND ${CMAKE_CTEST_COMMAND}
            --output-on-failure
            -R "^chronon3d_text_health_tests$"
        DEPENDS chronon3d_text_health_tests
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        USES_TERMINAL
    )

    chronon3d_add_test_suite(
        NAME chronon3d_mtsdf_generator_tests
        TIER UNIT
        SOURCES text/test_mtsdf_generator.cpp
        EXTRA_LINK_TARGETS chronon3d_backend_text
        LABELS text-mtsdf
    )

    # Content-addressed prepared-font identity: the glyph/face caches must key
    # on the font's content digest, not on its path.
    chronon3d_add_test_suite(
        NAME chronon3d_mtsdf_font_identity_tests
        TIER UNIT
        SOURCES text/test_mtsdf_font_identity.cpp
        EXTRA_LINK_TARGETS chronon3d_backend_text
        LABELS text-mtsdf
    )
endif()

if(CHRONON3D_BUILD_TESTS AND CHRONON3D_ENABLE_TEXT)
    chronon3d_add_test_suite(
        NAME chronon3d_text_fallback_tests
        TIER INTEGRATION
        SOURCES text/test_font_fallback_resolver.cpp
                render_graph/nodes/test_text_upload_scratch_reuse.cpp
    )
endif()

chronon3d_add_test_suite(
    NAME chronon3d_text_definition_tests
    TIER UNIT
    LINK_TARGETS chronon3d_pipeline
    SOURCES text/test_text_definition_canonical.cpp
)

chronon3d_add_test_suite(
    NAME chronon3d_safe_area_placement_tests
    TIER UNIT
    LINK_TARGETS chronon3d_text_core chronon3d_scene chronon3d_core chronon3d_sdk
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_safe_area_placement.cpp
)
list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_safe_area_placement_tests)

if(CHRONON3D_BUILD_DIAGNOSTICS)
    chronon3d_add_test_suite(
        NAME chronon3d_text_clip_policy_tests
        TIER INTEGRATION
        LINK_TARGETS chronon3d_pipeline chronon3d_backend_software chronon3d_scene chronon3d_text_core
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_clip_policy.cpp
    )
    list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_text_clip_policy_tests)

    chronon3d_add_test_suite(
        NAME chronon3d_visibility_contract_tests
        TIER UNIT
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_visibility_contract.cpp
    )
    if(TARGET chronon3d_backend_text)
        target_link_libraries(chronon3d_visibility_contract_tests PRIVATE chronon3d_backend_text)
    endif()
    list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_visibility_contract_tests)
endif()

chronon3d_add_test_suite(
    NAME chronon3d_text_layout_advanced_tests
    TIER UNIT
    LINK_TARGETS chronon3d_text_core chronon3d_scene chronon3d_core chronon3d_sdk
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/text/test_advanced_layout_matrix.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_layout_helpers.cpp
)
list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_text_layout_advanced_tests)

if(CHRONON3D_BUILD_TESTS)
    chronon3d_add_test_suite(
        NAME chronon3d_text_alignment_isolated_tests
        TIER INTEGRATION
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_alignment_isolated.cpp
        LINK_TARGETS chronon3d_pipeline chronon3d_backend_software
    )
    chronon3d_add_test_suite(
        NAME chronon3d_text_unicode_line_breaking_tests
        TIER UNIT
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_unicode_line_breaking.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/text/test_unicode_decoder.cpp
        LINK_TARGETS chronon3d_text_core chronon3d_pipeline chronon3d_backend_software
    )
    chronon3d_add_test_suite(
        NAME chronon3d_text_clip_oversized_tests
        TIER INTEGRATION
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_clip_oversized.cpp
        LINK_TARGETS chronon3d_pipeline chronon3d_backend_software
    )
    chronon3d_add_test_suite(
        NAME chronon3d_text_word_emphasis_animators_tests
        TIER UNIT
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_word_emphasis_animators.cpp
        LINK_TARGETS chronon3d_text_core
    )
    chronon3d_add_test_suite(
        NAME chronon3d_text_lightweight_emphasis_tests
        TIER UNIT
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_lightweight_emphasis_animators.cpp
        LINK_TARGETS chronon3d_text_core
    )
    chronon3d_add_test_suite(
        NAME chronon3d_subtitle_font_ref_tests
        TIER UNIT
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_subtitle_font_ref.cpp
        LINK_TARGETS chronon3d_pipeline
    )
endif()

# ── Re-registered text-domain suites (audit reconciliation 2026-09) ────────
# Suites dropped from registration by earlier refactors but still present and
# maintained on disk. Re-registered one-suite-per-file so each carries its own
# doctest binary (no cross-suite TEST_CASE collisions).
chronon3d_add_test_suite(
    NAME chronon3d_text_document_tests
    TIER UNIT
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_document.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_text_resolver_tests
    TIER INTEGRATION
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_resolver.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_single_line_composer_tests
    TIER UNIT
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_single_line_composer.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_paragraph_layout_extras_tests
    TIER UNIT
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_paragraph_layout_extras.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_every_line_composer_tests
    TIER UNIT
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_every_line_composer.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_path_sampler_tests
    TIER UNIT
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_path_sampler.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_text_path_composer_tests
    TIER UNIT
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_path_composer.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_text_run_builder_animated_doc_tests
    TIER INTEGRATION
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_run_builder_animated_doc.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_text_run_driver_tests
    TIER INTEGRATION
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_run_driver.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_prewarm_text_layout_cache_tests
    TIER INTEGRATION
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_prewarm_text_layout_cache.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_video_flicker_fps_tests
    TIER INTEGRATION
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_video_flicker_fps.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_text_animator_property_tests
    TIER UNIT
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/text_animator_property_tests.cpp
)
chronon3d_add_test_suite(
    NAME chronon3d_text_selector_evaluate_tests
    TIER UNIT
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_selector_evaluate.cpp
    EXTRA_LINK_TARGETS chronon3d_text_core
)
chronon3d_add_test_suite(
    NAME chronon3d_gpu_text_selector_metadata_tests
    TIER UNIT
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_gpu_text_selector_metadata.cpp
    EXTRA_LINK_TARGETS chronon3d_runtime
)
chronon3d_add_test_suite(
    NAME chronon3d_builder_consumed_commit_validation_tests
    TIER INTEGRATION
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_builder_consumed_commit_validation.cpp
)

# Domain labels for selective text validation: ctest -L text.
chronon3d_label_tests(text
    chronon3d_text_health_tests
    chronon3d_mtsdf_generator_tests
    chronon3d_mtsdf_font_identity_tests
    chronon3d_text_fallback_tests
    chronon3d_text_definition_tests
    chronon3d_safe_area_placement_tests
    chronon3d_text_clip_policy_tests
    chronon3d_visibility_contract_tests
    chronon3d_text_layout_advanced_tests
    chronon3d_text_alignment_isolated_tests
    chronon3d_text_unicode_line_breaking_tests
    chronon3d_text_clip_oversized_tests
    chronon3d_text_word_emphasis_animators_tests
    chronon3d_text_lightweight_emphasis_tests
    chronon3d_subtitle_font_ref_tests
    chronon3d_text_document_tests
    chronon3d_text_resolver_tests
    chronon3d_single_line_composer_tests
    chronon3d_paragraph_layout_extras_tests
    chronon3d_every_line_composer_tests
    chronon3d_path_sampler_tests
    chronon3d_text_path_composer_tests
    chronon3d_text_run_builder_animated_doc_tests
    chronon3d_text_run_driver_tests
    chronon3d_prewarm_text_layout_cache_tests
    chronon3d_video_flicker_fps_tests
    chronon3d_text_animator_property_tests
    chronon3d_text_selector_evaluate_tests
    chronon3d_gpu_text_selector_metadata_tests
    chronon3d_builder_consumed_commit_validation_tests)

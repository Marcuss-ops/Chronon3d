# Consolidated text-domain test registration.
# Preserves each former manifest's feature gates, target names, labels,
# sources and aggregate membership while keeping one CMake authority.

if(CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT)
    chronon3d_add_test_suite(
        NAME chronon3d_text_production_v1_tests
        TIER INTEGRATION
        LINK_TARGETS chronon3d_sdk chronon3d_software chronon3d_content
                     chronon3d_runtime chronon3d_text_core
        SOURCES certification/test_text_production_v1.cpp
    )
    target_compile_definitions(chronon3d_text_production_v1_tests PRIVATE
        CHRONON3D_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    set_tests_properties(chronon3d_text_production_v1_tests PROPERTIES
        LABELS "text-full-acceptance"
    )
endif()

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

chronon3d_add_test_suite(
    NAME chronon3d_text_rich_authoring_tests
    TIER UNIT
    LINK_TARGETS chronon3d_pipeline
    SOURCES text/test_text_rich_authoring.cpp
    LABELS text ungated
)

if(CHRONON3D_BUILD_DIAGNOSTICS)
    chronon3d_add_test_suite(
        NAME chronon3d_text_clip_policy_tests
        TIER INTEGRATION
        LINK_TARGETS chronon3d_pipeline chronon3d_backend_software chronon3d_scene chronon3d_text_core
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_clip_policy.cpp
    )
    list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_text_clip_policy_tests)
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
        LINK_TARGETS chronon3d_pipeline chronon3d_backend_software chronon3d_visual_test_support
    )
    chronon3d_add_test_suite(
        NAME chronon3d_text_auto_fit_tests
        TIER UNIT
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_auto_fit.cpp
        LINK_TARGETS chronon3d_pipeline
    )
    chronon3d_add_test_suite(
        NAME chronon3d_text_unicode_line_breaking_tests
        TIER UNIT
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_unicode_line_breaking.cpp
        LINK_TARGETS chronon3d_text_core chronon3d_pipeline chronon3d_backend_software
    )
    chronon3d_add_test_suite(
        NAME chronon3d_text_clip_oversized_tests
        TIER INTEGRATION
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_clip_oversized.cpp
        LINK_TARGETS chronon3d_pipeline chronon3d_backend_software chronon3d_visual_test_support
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

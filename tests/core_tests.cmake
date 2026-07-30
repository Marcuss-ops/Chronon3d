# Core test-suite orchestrator. Source ownership lives in manifests/.
if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/manifests/core_text_sources.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/manifests/core_general_sources.cmake)

chronon3d_add_test_suite(
    NAME chronon3d_core_tests
    TIER INTEGRATION
    LINK_TARGETS
        chronon3d_sdk
        chronon3d_sdk_impl
        chronon3d_pipeline
    SOURCES ${CORE_TEST_SOURCES}
)

if(CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D AND TARGET chronon3d_backend_text)
    target_link_libraries(chronon3d_core_tests PRIVATE chronon3d_backend_text)
endif()

target_compile_definitions(chronon3d_core_tests PRIVATE
    CMAKE_CURRENT_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
target_include_directories(chronon3d_core_tests PRIVATE ${CMAKE_SOURCE_DIR})

# Keep translation units with local fixture/type collisions out of unity builds.
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_compile_text_layout_errors.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_compile_text_layout_identity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_rich_text_paragraph_preservation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_compile_text_layout_per_paragraph_failure.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_unit_map_joint_include.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_unit_map_8level.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_crossfade_stroke.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_draw_text_run_crossfade_stroke.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_font_io_fence.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_draw_text_run_scratch_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_text_preset_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/test_camera_session_cache_failed_no_commit_session_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/registry/test_text_preset_descriptor.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

if(CHRONON3D_BUILD_CONTENT AND TARGET chronon3d_content)
    target_sources(chronon3d_core_tests PRIVATE
        core/timeline/test_sequence_v2_compositions.cpp
    )
    target_link_libraries(chronon3d_core_tests PRIVATE chronon3d_content)
endif()

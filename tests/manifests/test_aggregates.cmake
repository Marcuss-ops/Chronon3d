# Aggregate build targets, umbrella labels, and architecture gates.
if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()

macro(chronon3d_append_target_if_present list_name target_name)
    if(TARGET ${target_name})
        list(APPEND ${list_name} ${target_name})
    endif()
endmacro()

if(CHRONON3D_REQUIRES_DESCRIPTOR_REGISTRATION AND TARGET chronon3d_core_tests)
    target_compile_options(chronon3d_core_tests PRIVATE
        -Werror=deprecated-declarations
    )
endif()

add_test(
    NAME verify_descriptor_registration_flag
    COMMAND bash -c "${CMAKE_COMMAND} -L ${CMAKE_BINARY_DIR} 2>/dev/null | grep -q 'CHRONON3D_REQUIRES_DESCRIPTOR_REGISTRATION'"
)
set_tests_properties(verify_descriptor_registration_flag PROPERTIES
    LABELS "build-flag"
)

if(TARGET chronon3d_benchmarks)
    set(CHRONON3D_BENCHMARK_DEP
        chronon3d_benchmarks
        chronon3d_scene_program_benchmarks
    )
else()
    set(CHRONON3D_BENCHMARK_DEP "")
endif()

set(CHRONON3D_FAST_TEST_DEPS
    chronon3d_core_tests
    chronon3d_scene_tests
    chronon3d_optimizer_tests
    chronon3d_cache_tests
    chronon3d_compositor_tests
    chronon3d_timeline_tests
)
chronon3d_append_target_if_present(CHRONON3D_FAST_TEST_DEPS chronon3d_authoring_tests)
chronon3d_append_target_if_present(CHRONON3D_FAST_TEST_DEPS chronon3d_text_health_tests)
chronon3d_append_target_if_present(CHRONON3D_FAST_TEST_DEPS chronon3d_render_job_contract_tests)
if(CHRONON3D_USE_BLEND2D)
    chronon3d_append_target_if_present(CHRONON3D_FAST_TEST_DEPS chronon3d_deterministic_tests)
endif()
chronon3d_append_target_if_present(CHRONON3D_FAST_TEST_DEPS chronon3d_cli_tests)
chronon3d_append_target_if_present(CHRONON3D_FAST_TEST_DEPS chronon3d_precomp_focus_tests)
add_custom_target(chronon3d_tests_fast DEPENDS ${CHRONON3D_FAST_TEST_DEPS})

set(CHRONON3D_RENDER_TEST_DEPS "")
foreach(_target IN ITEMS
    chronon3d_renderer_tests
    chronon3d_io_tests
    chronon3d_animation_tests
    chronon3d_content_tests
    chronon3d_breathing_golden_tests
    chronon3d_visual_test_support_tests
    chronon3d_camera_visual_tests
    chronon3d_cinematic_motion_visual_tests
    chronon3d_gradient_visual_tests
    chronon3d_rounded_rect_visual_tests
    chronon3d_render_graph_node_visual_tests
    chronon3d_pr3_composition_visual_tests
    chronon3d_text_preset_visual_tests
    chronon3d_text_golden_tests
    chronon3d_presets_golden_tests
    chronon3d_diagnostic_overlay_tests
)
    chronon3d_append_target_if_present(CHRONON3D_RENDER_TEST_DEPS ${_target})
endforeach()
if(CHRONON3D_BUILD_CONTENT)
    chronon3d_append_target_if_present(
        CHRONON3D_RENDER_TEST_DEPS
        chronon3d_cinematic_camera_showcase_tests
    )
endif()
add_custom_target(chronon3d_tests_render DEPENDS ${CHRONON3D_RENDER_TEST_DEPS})

if(TARGET chronon3d_media_video_tests)
    add_custom_target(chronon3d_tests_video DEPENDS chronon3d_media_video_tests)
else()
    add_custom_target(chronon3d_tests_video)
endif()

add_custom_target(chronon3d_tests
    DEPENDS
        chronon3d_tests_fast
        chronon3d_tests_render
        chronon3d_tests_video
        ${CHRONON3D_BENCHMARK_DEP}
)
if(TARGET chronon3d_media_video_tests)
    add_dependencies(chronon3d_tests chronon3d_media_video_tests)
endif()

set(CHRONON3D_TEXT_FULL_ACCEPTANCE_DEPS "")
foreach(_target IN ITEMS
    chronon3d_text_presets_stability_tests
    chronon3d_text_simplicity_adapters_tests
    chronon3d_text_preset_visual_tests
    chronon3d_text_golden_tests
    chronon3d_text_builder_ergonomics_tests
    chronon3d_text_definition_tests
    chronon3d_text_rich_authoring_tests
    chronon3d_inspect_text_tests
)
    chronon3d_append_target_if_present(CHRONON3D_TEXT_FULL_ACCEPTANCE_DEPS ${_target})
endforeach()
add_custom_target(chronon3d_text_full_acceptance
    DEPENDS ${CHRONON3D_TEXT_FULL_ACCEPTANCE_DEPS}
)
foreach(_target IN LISTS CHRONON3D_TEXT_FULL_ACCEPTANCE_DEPS)
    set_tests_properties(${_target} PROPERTIES LABELS "text-full-acceptance")
endforeach()

set(CHRONON3D_SANITIZER_SUBSYSTEMS_DEPS "")
foreach(_target IN ITEMS
    chronon3d_core_tests
    chronon3d_text_presets_stability_tests
    chronon3d_visibility_contract_tests
    chronon3d_text_clip_policy_tests
    chronon3d_inspect_text_tests
)
    chronon3d_append_target_if_present(CHRONON3D_SANITIZER_SUBSYSTEMS_DEPS ${_target})
endforeach()
add_custom_target(chronon3d_sanitizer_subsystems
    DEPENDS ${CHRONON3D_SANITIZER_SUBSYSTEMS_DEPS}
)
foreach(_target IN LISTS CHRONON3D_SANITIZER_SUBSYSTEMS_DEPS)
    get_test_property(${_target} LABELS _existing_labels)
    if(_existing_labels)
        set_tests_properties(${_target} PROPERTIES
            LABELS "${_existing_labels};sanitizer-subsystems")
    else()
        set_tests_properties(${_target} PROPERTIES
            LABELS "sanitizer-subsystems")
    endif()
endforeach()

include(${CMAKE_CURRENT_LIST_DIR}/../architecture_tests.cmake)

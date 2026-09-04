# Aggregate build targets, umbrella labels, and architecture gates.
if(NOT CHRONON3D_BUILD_TESTS)
    return()
endif()

macro(chronon3d_append_target_if_present list_name target_name)
    if(TARGET ${target_name})
        list(APPEND ${list_name} ${target_name})
    endif()
endmacro()

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
    chronon3d_animation_helpers_tests
)
chronon3d_append_target_if_present(CHRONON3D_FAST_TEST_DEPS chronon3d_authoring_tests)
chronon3d_append_target_if_present(CHRONON3D_FAST_TEST_DEPS chronon3d_text_health_tests)
chronon3d_append_target_if_present(CHRONON3D_FAST_TEST_DEPS chronon3d_render_job_contract_tests)
if(CHRONON3D_USE_BLEND2D)
    chronon3d_append_target_if_present(CHRONON3D_FAST_TEST_DEPS chronon3d_deterministic_tests)
    chronon3d_append_target_if_present(CHRONON3D_FAST_TEST_DEPS chronon3d_precomp_tests)
endif()
chronon3d_append_target_if_present(CHRONON3D_FAST_TEST_DEPS chronon3d_cli_tests)
add_custom_target(chronon3d_tests_fast DEPENDS ${CHRONON3D_FAST_TEST_DEPS})

foreach(_target IN LISTS CHRONON3D_FAST_TEST_DEPS)
    if(TARGET ${_target})
        get_test_property(${_target} LABELS _dev_fast_existing_labels)
        if(_dev_fast_existing_labels)
            set_tests_properties(${_target} PROPERTIES
                LABELS "${_dev_fast_existing_labels};dev-fast")
        else()
            set_tests_properties(${_target} PROPERTIES LABELS "dev-fast")
        endif()
    endif()
endforeach()

set(CHRONON3D_RENDER_TEST_DEPS "")
foreach(_target IN ITEMS
    chronon3d_renderer_tests
    chronon3d_io_tests
    chronon3d_animation_tests
    chronon3d_precomp_tests
)
    chronon3d_append_target_if_present(CHRONON3D_RENDER_TEST_DEPS ${_target})
endforeach()
add_custom_target(chronon3d_tests_render DEPENDS ${CHRONON3D_RENDER_TEST_DEPS})

# Every executable registered through chronon3d_add_test_suite() is a mandatory
# dependency of the canonical aggregate. Focused aggregates select subsets only.
get_property(CHRONON3D_ALL_REGISTERED_TEST_TARGETS
    GLOBAL
    PROPERTY CHRONON3D_ALL_TEST_TARGETS
)
set(CHRONON3D_ALL_TEST_DEPS "")
foreach(_target IN LISTS CHRONON3D_ALL_REGISTERED_TEST_TARGETS)
    chronon3d_append_target_if_present(CHRONON3D_ALL_TEST_DEPS ${_target})
endforeach()

if(TARGET chronon3d_media_video_tests)
    set(_CHRONON3D_VIDEO_TEST_DEPS chronon3d_media_video_tests)
    if(TARGET chronon3d_native_decoder_tests)
        list(APPEND _CHRONON3D_VIDEO_TEST_DEPS chronon3d_native_decoder_tests)
    endif()
    add_custom_target(chronon3d_tests_video DEPENDS ${_CHRONON3D_VIDEO_TEST_DEPS})
else()
    add_custom_target(chronon3d_tests_video)
endif()

add_custom_target(chronon3d_tests
    DEPENDS
        ${CHRONON3D_BENCHMARK_DEP}
        ${CHRONON3D_ALL_TEST_DEPS}
)

set(CHRONON3D_TEXT_FULL_ACCEPTANCE_DEPS "")
foreach(_target IN ITEMS
    chronon3d_text_definition_tests
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

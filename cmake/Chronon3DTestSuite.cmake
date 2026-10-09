# Canonical test-suite registration helper.
# Test target membership is tracked once through CHRONON3D_ALL_TEST_TARGETS;
# focused aggregates may select subsets, but they are not parallel authorities.

set(_CHRONON3D_TIER_DEFAULT_LINKS_UNIT "chronon3d_pipeline")
set(_CHRONON3D_TIER_DEFAULT_LINKS_INTEGRATION
    "chronon3d_pipeline;chronon3d_backend_software")
set(_CHRONON3D_TIER_DEFAULT_LINKS_SDK "chronon3d_sdk")

function(chronon3d_add_test_suite)
    cmake_parse_arguments(
        ARG
        "NO_PIPELINE;NO_DOCTEST"
        "NAME;TIER"
        "SOURCES;LINK_TARGETS;EXTRA_LINK_TARGETS;LABELS"
        ${ARGN}
    )

    if(NOT ARG_NAME)
        message(FATAL_ERROR
            "chronon3d_add_test_suite: NAME is required")
    endif()

    if(NOT ARG_TIER)
        message(FATAL_ERROR
            "chronon3d_add_test_suite: TIER is required for '${ARG_NAME}'")
    endif()

    if(NOT ARG_TIER STREQUAL "UNIT" AND
       NOT ARG_TIER STREQUAL "INTEGRATION" AND
       NOT ARG_TIER STREQUAL "SDK")
        message(FATAL_ERROR
            "chronon3d_add_test_suite: invalid TIER='${ARG_TIER}' for '${ARG_NAME}'")
    endif()

    if(ARG_NO_PIPELINE AND NOT ARG_LINK_TARGETS)
        message(FATAL_ERROR
            "chronon3d_add_test_suite: NO_PIPELINE requires explicit LINK_TARGETS for '${ARG_NAME}'")
    endif()

    if(NOT ARG_LINK_TARGETS)
        set(ARG_LINK_TARGETS "${_CHRONON3D_TIER_DEFAULT_LINKS_${ARG_TIER}}")
    elseif(NOT ARG_NO_PIPELINE)
        message(AUTHOR_WARNING
            "chronon3d_add_test_suite: LINK_TARGETS override on '${ARG_NAME}' "
            "(tier='${ARG_TIER}'); prefer EXTRA_LINK_TARGETS for additive dependencies")
    endif()

    if(ARG_EXTRA_LINK_TARGETS)
        list(APPEND ARG_LINK_TARGETS ${ARG_EXTRA_LINK_TARGETS})
        list(REMOVE_DUPLICATES ARG_LINK_TARGETS)
    endif()

    # Internal suites never consume the monolithic SDK archive. Keep the
    # aggregate pipeline as the canonical internal link boundary.
    if(NOT ARG_TIER STREQUAL "SDK" AND NOT ARG_NO_PIPELINE)
        list(REMOVE_ITEM ARG_LINK_TARGETS chronon3d_sdk chronon3d_sdk_impl)
        list(FIND ARG_LINK_TARGETS chronon3d_pipeline _pipeline_index)
        if(_pipeline_index EQUAL -1)
            list(APPEND ARG_LINK_TARGETS chronon3d_pipeline)
        endif()
    endif()

    if(ARG_NO_DOCTEST)
        # Standalone-main suites (ADR-034 style) bring their own entry point;
        # the shared doctest main would collide with it at link time.
        add_executable(${ARG_NAME} ${ARG_SOURCES})
    else()
        add_executable(${ARG_NAME} ${TEST_MAIN} ${ARG_SOURCES})
    endif()

    # Test files commonly contain same-named local fixtures; keep test
    # executables out of the project-wide unity build at the suite boundary.
    set_target_properties(${ARG_NAME} PROPERTIES UNITY_BUILD OFF)

    target_link_libraries(${ARG_NAME} PRIVATE
        ${ARG_LINK_TARGETS}
    )
    if(NOT ARG_NO_DOCTEST)
        target_link_libraries(${ARG_NAME} PRIVATE doctest::doctest)
    endif()

    target_include_directories(${ARG_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/tests
    )

    add_test(
        NAME ${ARG_NAME}
        COMMAND $<TARGET_FILE:${ARG_NAME}>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )

    if(ARG_LABELS)
        set_tests_properties(${ARG_NAME} PROPERTIES LABELS "${ARG_LABELS}")
    endif()

    set_property(
        GLOBAL APPEND
        PROPERTY CHRONON3D_ALL_TEST_TARGETS
        "${ARG_NAME}"
    )
endfunction()

# Apply a domain label to suites that are enabled by the current configure.
# Optional test registrations may be absent, so this helper deliberately
# ignores names that were gated out instead of making configure fail.
function(chronon3d_label_tests label)
    foreach(test_name IN LISTS ARGN)
        if(TEST "${test_name}")
            set_tests_properties(${test_name} PROPERTIES LABELS "${label}")
        endif()
    endforeach()
endfunction()

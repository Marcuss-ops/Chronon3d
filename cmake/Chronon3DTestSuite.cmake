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
        "NO_PIPELINE"
        "NAME;TIER"
        "SOURCES;LINK_TARGETS;LABELS"
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
    else()
        message(AUTHOR_WARNING
            "chronon3d_add_test_suite: LINK_TARGETS override on '${ARG_NAME}' "
            "(tier='${ARG_TIER}')")
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

    add_executable(${ARG_NAME} ${TEST_MAIN} ${ARG_SOURCES})

    # Test files commonly contain same-named local fixtures; keep test
    # executables out of the project-wide unity build at the suite boundary.
    set_target_properties(${ARG_NAME} PROPERTIES UNITY_BUILD OFF)

    target_link_libraries(${ARG_NAME} PRIVATE
        ${ARG_LINK_TARGETS}
        doctest::doctest
    )

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

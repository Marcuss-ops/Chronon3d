# -- Broad CLI Tests --
if(NOT CHRONON3D_BUILD_CLI OR NOT TARGET chronon3d_cli_render)
    return()
endif()

set(_cli_tests_link_targets
    chronon3d_cli_render
    chronon3d_cli_core
    chronon3d_sdk
    chronon3d_sdk_impl
    chronon3d_pipeline
    chronon3d_scene
    chronon3d_backend_software
    chronon3d_backend_image
    CLI11::CLI11
    fmt::fmt
)

set(_cli_test_sources
    cli/test_frame_range_parser.cpp
    cli/test_render_job_planning.cpp
    cli/test_render_error_formatter.cpp
    cli/bench_json_tests.cpp
    cli/test_camera_path_command.cpp
    cli/test_populate_run_host_attribs.cpp
    cli/test_render_job_write_frame_sanity.cpp
    cli/test_semantic_script.cpp
)

# Video exporter tests are appended only when the implementation target exists.
# The removed video command target must never be reintroduced here.
if(TARGET chronon3d_cli_video_export)
    list(APPEND _cli_tests_link_targets
        chronon3d_cli_video_export
        chronon3d_media_video
    )
    list(APPEND _cli_test_sources
        ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/commands/video/common/video_export_common.cpp
        cli/test_pipe_export_helpers.cpp
        cli/test_video_end_semantics.cpp
        cli/test_video_sink_encoders.cpp
        cli/test_ffmpeg_export_options.cpp
        cli/test_packet_assembler_contract.cpp
    )
endif()

if(TARGET chronon3d_cli_dev)
    list(APPEND _cli_tests_link_targets chronon3d_cli_dev)
endif()

chronon3d_add_test_suite(
    NAME chronon3d_cli_tests
    TIER SDK
    LINK_TARGETS ${_cli_tests_link_targets}
    SOURCES ${_cli_test_sources}
            cli/test_render_request.cpp
)

target_include_directories(chronon3d_cli_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli
)

if(CHRONON3D_ENABLE_CUDA_INTEROP AND CHRONON3D_CUDA_INCLUDE_DIR)
    target_include_directories(chronon3d_cli_tests PRIVATE
        "${CHRONON3D_CUDA_INCLUDE_DIR}"
    )
endif()

# Canonical RenderJob planner/executor contract remains a focused target so
# matrix builds can certify it independently from the broad CLI executable.
set(_render_job_contract_links
    chronon3d_cli_render
    chronon3d_cli_core
    chronon3d_sdk
    chronon3d_sdk_impl
    chronon3d_pipeline
    chronon3d_scene
    chronon3d_backend_software
    chronon3d_backend_image
    CLI11::CLI11
    fmt::fmt
)
if(TARGET chronon3d_cli_video_export)
    list(APPEND _render_job_contract_links
        chronon3d_cli_video_export
        chronon3d_media_video
    )
endif()
chronon3d_add_test_suite(
    NAME chronon3d_render_job_contract_tests
    TIER SDK
    LINK_TARGETS ${_render_job_contract_links}
    SOURCES cli/test_render_job_planning.cpp
)
target_include_directories(chronon3d_render_job_contract_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli
)
if(NOT TARGET chronon3d_cli_video_export)
    target_compile_definitions(chronon3d_render_job_contract_tests PRIVATE
        CHRONON3D_TEST_VIDEO_EXPORT_DISABLED=1
    )
endif()

# Developer CLI introspection commands are optional but share CLI ownership.
if(CHRONON3D_BUILD_CLI_DEV)
    chronon3d_add_test_suite(
        NAME chronon3d_introspection_tests
        TIER UNIT
        LINK_TARGETS chronon3d_cli_dev
        SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/cli/test_schema_command.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/cli/test_example_props_command.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/cli/test_validate_command.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/cli/test_resolve_command.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/cli/test_props_inline.cpp
    )
    list(APPEND CHRONON3D_FAST_TEST_DEPS chronon3d_introspection_tests)
endif()

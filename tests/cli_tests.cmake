# -- CLI Tests --
# Per-area early-return gate (TICKET-CMAKE-TEST-MANIFEST-UNIFICATION).
# The canonical planner/executor belongs to chronon3d_cli_render and must be
# testable in both image-only and video-enabled configurations.
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
    cli/test_render_loop_integration.cpp
    cli/bench_json_tests.cpp
    cli/test_camera_path_command.cpp
    cli/test_populate_run_host_attribs.cpp
    # TICKET-RENDER-PIPELINE-INTEGRITY layer 1 — pre-write framebuffer
    # sanity scan (alpha-zero > 0.85, NaN/Inf > 0.05).
    cli/test_render_job_write_frame_sanity.cpp
)

# Video exporter tests are appended only when the implementation target exists.
# The removed chronon3d_cli_video command target must never be reintroduced here.
if(TARGET chronon3d_cli_video_export)
    list(APPEND _cli_tests_link_targets
        chronon3d_cli_video_export
        chronon3d_media_video
    )
    list(APPEND _cli_test_sources
        cli/test_frame_chunks.cpp
        cli/test_pipe_export_helpers.cpp
        cli/test_video_end_semantics.cpp
        cli/test_video_sink_encoders.cpp
        cli/test_ffmpeg_export_options.cpp
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
)

# CLI tests have a non-standard include dir for production CLI sources.
target_include_directories(chronon3d_cli_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli
)

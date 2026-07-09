# -- CLI Tests --
if(CHRONON3D_BUILD_CLI AND CHRONON3D_ENABLE_VIDEO)

# chronon3d_cli_dev is gated by `option(CHRONON3D_BUILD_CLI_DEV ... OFF)` in
# apps/chronon3d_cli/CMakeLists.txt:20 — the target is registered only when
# that option is ON (e.g. a `linux-dev` preset).  On presets where the option
# is OFF (e.g. linux-content-dev), the static library does not exist and the
# link step fails.  Build the link list with the set/list(APPEND) idiom
# rather than embedding an if()/endif() inside the LINK_TARGETS argument
# list, because `chronon3d_add_test_suite` parses LINK_TARGETS via
# cmake_parse_arguments and cannot correctly handle nested conditional
# blocks in that slot.
set(_cli_tests_link_targets
    chronon3d_cli_video chronon3d_cli_render chronon3d_cli_core
    chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline
    chronon3d_scene chronon3d_backend_software
    chronon3d_media_video chronon3d_backend_image
    CLI11::CLI11 fmt::fmt)
if(TARGET chronon3d_cli_dev)
    list(APPEND _cli_tests_link_targets chronon3d_cli_dev)
endif()

chronon3d_add_test_suite(
    NAME chronon3d_cli_tests
    TIER SDK
    LINK_TARGETS ${_cli_tests_link_targets}
    SOURCES cli/test_frame_range_parser.cpp
            cli/test_frame_chunks.cpp
            cli/test_pipe_export_helpers.cpp
            cli/test_render_loop_integration.cpp
            cli/bench_json_tests.cpp
            cli/test_camera_path_command.cpp
            cli/test_video_end_semantics.cpp
            cli/test_video_sink_encoders.cpp
            cli/test_ffmpeg_export_options.cpp
            cli/test_populate_run_host_attribs.cpp
            # TICKET-RENDER-PIPELINE-INTEGRITY layer 1 — pre-write framebuffer
            # sanity scan (alpha-zero > 0.85, NaN/Inf > 0.05).
            cli/test_render_job_write_frame_sanity.cpp
)
# CLI tests have a non-standard include dir for the chrono3d_cli
# production sources; explicit add on top of the helper's default.
target_include_directories(chronon3d_cli_tests PRIVATE ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli)

endif()

# -- CLI Tests --
if(CHRONON3D_BUILD_CLI AND CHRONON3D_ENABLE_VIDEO)

chronon3d_add_test_suite(
    NAME chronon3d_cli_tests
    TIER SDK
    LINK_TARGETS chronon3d_cli_video chronon3d_cli_render chronon3d_cli_core chronon3d_cli_dev
                  chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline
                  chronon3d_scene chronon3d_backend_software
                  chronon3d_media_video chronon3d_backend_image
                  CLI11::CLI11 fmt::fmt
    SOURCES cli/test_frame_range_parser.cpp
            cli/test_frame_chunks.cpp
            cli/test_pipe_export_helpers.cpp
            cli/test_render_loop_integration.cpp
            cli/bench_json_tests.cpp
            cli/test_camera_path_command.cpp
            cli/test_video_end_semantics.cpp
            cli/test_video_sink_encoders.cpp
            cli/test_ffmpeg_export_options.cpp
)
# CLI tests have a non-standard include dir for the chrono3d_cli
# production sources; explicit add on top of the helper's default.
target_include_directories(chronon3d_cli_tests PRIVATE ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli)

endif()

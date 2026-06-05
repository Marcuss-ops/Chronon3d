# ── CLI Tests ──

add_executable(chronon3d_cli_tests
    ${TEST_MAIN}
    cli/test_frame_range_parser.cpp
    cli/test_frame_chunks.cpp
    cli/test_ffmpeg_pipe_encoder.cpp
    cli/test_video_pipeline_producer_consumer.cpp
    cli/test_pipe_export_helpers.cpp
    cli/test_pipe_export_session.cpp
    cli/test_render_loop_integration.cpp
    cli/bench_json_tests.cpp
    cli/test_camera_path_command.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/utils/common/cli_utils.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/commands/video/video_export_common.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/commands/video/pipe_export_helpers.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/commands/video/pipe_export_session.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/utils/video/ffmpeg_pipe_write_frame.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder_uring.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/utils/video/ffmpeg_pipe_command.cpp
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/utils/video/ffmpeg_pipe_yuv.cpp
)

target_link_libraries(chronon3d_cli_tests
    PRIVATE
        chronon3d_pipeline
        chronon3d_scene
        chronon3d_backend_software
        CLI11::CLI11
        doctest::doctest
        fmt::fmt
)
target_include_directories(chronon3d_cli_tests PRIVATE ${CMAKE_SOURCE_DIR})
chronon3d_enable_test_pch(chronon3d_cli_tests)
add_test(NAME chronon3d_cli_tests COMMAND chronon3d_cli_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

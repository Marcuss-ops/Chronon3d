# -- CLI Tests --

add_executable(chronon3d_cli_tests
    ${TEST_MAIN}
    cli/test_frame_range_parser.cpp
    cli/test_frame_chunks.cpp
    cli/test_pipe_export_helpers.cpp
    cli/test_render_loop_integration.cpp
    cli/bench_json_tests.cpp
    cli/test_camera_path_command.cpp
    cli/test_video_end_semantics.cpp
    cli/test_video_sink_encoders.cpp
)

target_link_libraries(chronon3d_cli_tests
    PRIVATE
        chronon3d_cli_video
        chronon3d_cli_render
        chronon3d_cli_core
        chronon3d_cli_dev
        chronon3d_pipeline
        chronon3d_scene
        chronon3d_backend_software
        chronon3d_cli_core
        chronon3d_cli_video
        chronon3d_cli_render
        chronon3d_cli_dev
        chronon3d_media_video
        chronon3d_backend_image
        CLI11::CLI11
        doctest::doctest
        fmt::fmt
)
target_include_directories(chronon3d_cli_tests PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli)
chronon3d_enable_test_pch(chronon3d_cli_tests)
add_test(NAME chronon3d_cli_tests COMMAND chronon3d_cli_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

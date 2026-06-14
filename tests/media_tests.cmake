# ── Media/Video Tests (isolated, no chronon3d_pipeline) ──
#
# These tests only exercise chronon3d_media_video and chronon3d_backend_video.
# They do NOT depend on chronon3d_pipeline (no scene graph, no renderer).
# This keeps build time low and prevents pipeline changes from breaking
# video-sink / frame-converter tests.

add_executable(chronon3d_media_video_tests
    ${TEST_MAIN}
    video/test_frame_converter.cpp
    video/test_video_diff.cpp
    video/test_long_export.cpp
    video/test_near_static_frames.cpp
    video/test_raw_video_sink.cpp
    video/test_ffmpeg_pipe_sink.cpp
)
target_link_libraries(chronon3d_media_video_tests
    PRIVATE
        chronon3d_backend_video
        chronon3d_backend_software
        chronon3d_media_video
        doctest::doctest
)
# Disable unity build because multiple test files define helper functions
# (temp_path, make_config, fill_pattern) in anonymous namespaces.
set_target_properties(chronon3d_media_video_tests PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_media_video_tests PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME chronon3d_media_video_tests COMMAND chronon3d_media_video_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

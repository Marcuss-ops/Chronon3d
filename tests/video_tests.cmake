# ── Video Tests (Isolated) ──

add_executable(chronon3d_video_tests
    ${TEST_MAIN}
    video/test_frame_converter.cpp
    video/test_video_diff.cpp
    video/test_long_export.cpp
    video/test_near_static_frames.cpp
    video/test_raw_video_sink.cpp
)
target_link_libraries(chronon3d_video_tests
    PRIVATE
        chronon3d_pipeline
        chronon3d_media_video
        doctest::doctest
)
target_include_directories(chronon3d_video_tests PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME chronon3d_video_tests COMMAND chronon3d_video_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

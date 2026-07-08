# ── Media/Video Tests (isolated, no chronon3d_pipeline) ──
#
# These tests only exercise chronon3d_media_video and chronon3d_backend_video.
# They do NOT depend on chronon3d_pipeline (no scene graph, no renderer).
# This keeps build time low and prevents pipeline changes from breaking
# video-sink / frame-converter tests.

chronon3d_add_test_suite(
    NAME chronon3d_media_video_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_backend_video chronon3d_backend_software chronon3d_media_video chronon3d_scene
    SOURCES video/test_frame_converter.cpp
            video/test_converted_frame_cache.cpp
            video/test_video_diff.cpp
            video/test_long_export.cpp
            video/test_near_static_frames.cpp
            video/test_raw_video_sink_lifecycle.cpp
            video/test_raw_video_sink_submit.cpp
            video/test_raw_video_sink_planar.cpp
            video/test_raw_video_sink_edge.cpp
            video/test_ffmpeg_pipe_sink_lifecycle.cpp
            video/test_ffmpeg_pipe_sink_submit.cpp
            video/test_ffmpeg_pipe_sink_edge.cpp
            video/reference_yuv_converter.cpp
            cli/test_video_adapter_e2e.cpp
            ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli/utils/video/video_sink_adapter.cpp
)
# CLI tests have a non-standard include dir for chrono3d_cli production
# sources (the chronon3d_media_video_tests target reuses the CLI's
# video_sink_adapter.cpp from apps/chronon3d_cli/utils/video/).
# chronon3d_add_test_suite() already sets UNITY_BUILD OFF + the standard
# include dirs; this explicit add covers the /apps/chronon3d_cli subset
# the test sources need to compile.
target_include_directories(chronon3d_media_video_tests PRIVATE ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli)

# ── Media/Video Tests ──
#
# These tests exercise chronon3d_media_video, chronon3d_backend_video,
# and the video_sink_adapter from the CLI.
#
# NOTE: chronon3d_backend_software is an OBJECT library whose transitive
# dependencies (chronon3d, chronon3d_graph, chronon3d_cache, chronon3d_effects,
# blend2d, backend_text) do NOT propagate via target_link_libraries.
# We must link chronon3d_pipeline explicitly to resolve all symbols.
# This matches the standard INTEGRATION tier link contract.

chronon3d_add_test_suite(
    NAME chronon3d_media_video_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_pipeline chronon3d_backend_video chronon3d_backend_software chronon3d_media_video chronon3d_scene
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
            video/test_frame_rate_video_validation.cpp
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

# Backend text + Blend2D are needed when those features are enabled
# (transitive deps of chronon3d_backend_software that don't propagate
# from the OBJECT library link).
if(CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D AND TARGET chronon3d_backend_text)
    target_link_libraries(chronon3d_media_video_tests PRIVATE chronon3d_backend_text)
endif()
if(CHRONON3D_USE_BLEND2D AND TARGET blend2d::blend2d)
    target_link_libraries(chronon3d_media_video_tests PRIVATE blend2d::blend2d)
endif()

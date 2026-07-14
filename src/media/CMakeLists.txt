# ==============================================================================
# src/media/CMakeLists.txt — chronon3d_media_interface + chronon3d_backend_video
#
# INTERFACE include dirs must use generator expressions so the installed
# Chronon3DTargets.cmake substitutes the install-time path (`include/`)
# instead of leaking the source-tree path.
# ==============================================================================

# Item 19: media interface target (header-only — abstract provider contract)
add_library(chronon3d_media_interface INTERFACE)
target_include_directories(chronon3d_media_interface INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(chronon3d_media_interface INTERFACE chronon3d)

if(CHRONON3D_ENABLE_VIDEO)
    # TICKET-011 cmake-boundary (OBJECT lift) — see chronon3d_backend_software comment.
    add_library(chronon3d_backend_video OBJECT
        frame_conversion/frame_converter.cpp
        frame_conversion/backends/packed_backend.cpp
        frame_conversion/backends/swscale_backend.cpp
        frame_conversion/converted_frame_cache.cpp
        frame_conversion/frame_conversion_service.cpp
    )
    set_target_properties(chronon3d_backend_video PROPERTIES UNITY_BUILD OFF)
    target_link_libraries(chronon3d_backend_video PUBLIC
        chronon3d
        chronon3d_media_interface
        chronon3d_cache
        spdlog::spdlog_header_only
        fmt::fmt
        TBB::tbb
    )
    # ── chronon3d_media_video — Video sink library ──────────────────
    # TICKET-011 cmake-boundary (OBJECT lift) — see chronon3d_backend_software comment.
    add_library(chronon3d_media_video OBJECT
        video/raw_video_sink.cpp
        video/ffmpeg_pipe_sink.cpp
        video/ffmpeg_pipe_args.cpp
        video/ffmpeg_frame_submit.cpp
        video/process_runner.cpp
        video/process_runner_posix.cpp
        video/video_sink_factory.cpp
        video/video_config_validator.cpp
    )
    target_link_libraries(chronon3d_media_video PUBLIC
        chronon3d_media_interface
    )
    target_include_directories(chronon3d_media_video PRIVATE ${CMAKE_SOURCE_DIR})

    if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
        target_link_libraries(chronon3d_backend_video PUBLIC chronon3d_ffmpeg_light)
    endif()
    target_include_directories(chronon3d_backend_video PRIVATE ${CMAKE_SOURCE_DIR})
endif()

if(NOT (CHRONON3D_ENABLE_VIDEO AND TARGET chronon3d_cli_render))
    return()
endif()

add_library(chronon3d_cli_video_export STATIC
    commands/video/common/video_export_common.cpp
    commands/video/common/pipe_export_helpers.cpp
    commands/video/exporters/video_export_pipe.cpp
    commands/video/exporters/pipe_export_pipeline.cpp
    commands/video/exporters/pipe_export_finalize.cpp
    commands/video/common/pipe_export_session.cpp
    commands/video/exporters/video_export_chunked.cpp
    utils/video/video_sink_encoders.cpp
    utils/video/video_sink_adapter.cpp
)
target_include_directories(chronon3d_cli_video_export PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli
)
target_link_libraries(chronon3d_cli_video_export PRIVATE
    chronon3d_cli_core
    chronon3d_pipeline
    chronon3d_backend_image
    chronon3d_media_video
    CLI11::CLI11
    spdlog::spdlog_header_only
    fmt::fmt
)

if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    target_sources(chronon3d_cli_video_export PRIVATE
        utils/video/native_av_encoder.cpp
        utils/video/native_av_encoder_write.cpp
    )
    target_link_libraries(chronon3d_cli_video_export PRIVATE
        chronon3d_ffmpeg_full
    )
    target_compile_definitions(chronon3d_cli_video_export PRIVATE
        CHRONON3D_ENABLE_NATIVE_FFMPEG
    )
endif()

target_sources(chronon3d_cli_render PRIVATE
    utils/video/video_job_validate.cpp
    utils/video/video_job_dry_run.cpp
    utils/video/video_job_execute.cpp
)
target_link_libraries(chronon3d_cli_render PRIVATE
    chronon3d_cli_video_export
)
target_compile_definitions(chronon3d_cli_render PRIVATE
    CHRONON3D_HAS_CLI_VIDEO_EXPORT
)
target_compile_definitions(chronon3d_cli_core PRIVATE
    CHRONON3D_HAS_CLI_VIDEO_EXPORT
)

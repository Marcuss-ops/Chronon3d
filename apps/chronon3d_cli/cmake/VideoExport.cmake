if(NOT (CHRONON3D_ENABLE_VIDEO AND TARGET chronon3d_cli_render))
    return()
endif()

add_library(chronon3d_cli_video_export STATIC
    commands/video/common/pipe_export_helpers.cpp
    commands/video/exporters/pipe_export_pipeline.cpp
    commands/video/exporters/pipe_export_finalize.cpp
    commands/video/common/pipe_export_session.cpp
    utils/video/video_sink_encoders.cpp
    utils/video/video_sink_adapter.cpp
)
target_include_directories(chronon3d_cli_video_export PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli
)
target_link_libraries(chronon3d_cli_video_export PRIVATE
    CLI11::CLI11
    spdlog::spdlog_header_only
    fmt::fmt
)

if(CHRONON3D_ENABLE_TELEMETRY)
    target_compile_definitions(chronon3d_cli_video_export PRIVATE
        CHRONON3D_ENABLE_SQLITE_TELEMETRY
    )
endif()

if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    target_sources(chronon3d_cli_video_export PRIVATE
        utils/video/native_av_encoder.cpp
        utils/video/native_av_encoder_write.cpp
        utils/video/native_video_frame_decoder.cpp
    )
    target_link_libraries(chronon3d_cli_video_export PRIVATE
        chronon3d_ffmpeg_full
    )
    target_compile_definitions(chronon3d_cli_video_export PRIVATE
        CHRONON3D_ENABLE_NATIVE_FFMPEG
    )
endif()

if(CHRONON3D_ENABLE_CUDA_INTEROP AND CHRONON3D_ENABLE_NATIVE_FFMPEG)
    find_path(CHRONON3D_CUDA_INCLUDE_DIR_CLI cuda.h
        HINTS "$ENV{CUDA_HOME}" "$ENV{CUDA_PATH}"
        PATH_SUFFIXES include
        PATHS
            /usr/local/lib/python3.10/dist-packages/nvidia/cu13/include
            /tmp/velox-cuda-dev/extracted/usr/include)
    find_library(CHRONON3D_CUDA_DRIVER_LIBRARY_CLI cuda
        HINTS "$ENV{CUDA_HOME}" "$ENV{CUDA_PATH}"
        PATH_SUFFIXES lib64 lib/x64 lib
        PATHS /usr/lib/x86_64-linux-gnu)
    target_include_directories(chronon3d_cli_video_export PRIVATE
        "${CHRONON3D_CUDA_INCLUDE_DIR_CLI}")
    # NativeAvEncoder's public header is included by render_job_execute.cpp,
    # which belongs to chronon3d_cli_render. The CUDA interop type must be
    # visible on that compile target as well, not only on the video library.
    target_include_directories(chronon3d_cli_render PRIVATE
        "${CHRONON3D_CUDA_INCLUDE_DIR_CLI}")
    target_link_libraries(chronon3d_cli_video_export PRIVATE
        chronon3d_backend_vulkan "${CHRONON3D_CUDA_DRIVER_LIBRARY_CLI}")
endif()

target_sources(chronon3d_cli_render PRIVATE
    commands/video/common/video_export_common.cpp
    commands/video/exporters/video_export_pipe.cpp
    commands/video/exporters/video_export_chunked.cpp
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

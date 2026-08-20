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
    ${CMAKE_SOURCE_DIR}/include
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
        utils/video/cuda_nv12_surface_compositor.cpp
    )
    target_link_libraries(chronon3d_cli_video_export PRIVATE
        chronon3d_ffmpeg_full
    )
    target_compile_definitions(chronon3d_cli_video_export PRIVATE
        CHRONON3D_ENABLE_NATIVE_FFMPEG
    )
endif()

if(CHRONON3D_ENABLE_CUDA_INTEROP AND CHRONON3D_ENABLE_NATIVE_FFMPEG)
    # Prefer CMake's canonical CUDA package.  The fallback is intentionally
    # environment-driven; no host-specific Python wheel or temporary build
    # directory belongs in the source tree.
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        set(CHRONON3D_CUDA_INCLUDE_DIR_CLI "${CUDAToolkit_INCLUDE_DIRS}")
        set(CHRONON3D_NVRTC_INCLUDE_DIR_CLI "${CUDAToolkit_INCLUDE_DIRS}")
        set(CHRONON3D_CUDA_DRIVER_LIBRARY_CLI CUDA::cuda_driver)
        set(CHRONON3D_NVRTC_LIBRARY_CLI CUDA::nvrtc)
    else()
        find_path(CHRONON3D_CUDA_INCLUDE_DIR_CLI cuda.h
            HINTS "$ENV{CUDA_HOME}" "$ENV{CUDA_PATH}" "$ENV{CUDA_ROOT}"
                  "$ENV{CONDA_PREFIX}" "$ENV{VIRTUAL_ENV}"
            PATH_SUFFIXES include include/cuda)
        find_library(CHRONON3D_CUDA_DRIVER_LIBRARY_CLI cuda
            HINTS "$ENV{CUDA_HOME}" "$ENV{CUDA_PATH}" "$ENV{CUDA_ROOT}"
                  "$ENV{CONDA_PREFIX}" "$ENV{VIRTUAL_ENV}"
            PATH_SUFFIXES lib64 lib lib/x64)
        find_path(CHRONON3D_NVRTC_INCLUDE_DIR_CLI nvrtc.h
            HINTS "$ENV{CUDA_HOME}" "$ENV{CUDA_PATH}" "$ENV{CUDA_ROOT}"
                  "$ENV{CONDA_PREFIX}" "$ENV{VIRTUAL_ENV}"
            PATH_SUFFIXES include include/cuda)
        find_library(CHRONON3D_NVRTC_LIBRARY_CLI
            NAMES nvrtc libnvrtc.so
            HINTS "$ENV{CUDA_HOME}" "$ENV{CUDA_PATH}" "$ENV{CUDA_ROOT}"
                  "$ENV{CONDA_PREFIX}" "$ENV{VIRTUAL_ENV}"
            PATH_SUFFIXES lib64 lib lib/x64)
    endif()
    target_include_directories(chronon3d_cli_video_export PRIVATE
        "${CHRONON3D_CUDA_INCLUDE_DIR_CLI}"
        "${CHRONON3D_NVRTC_INCLUDE_DIR_CLI}")
    # NativeAvEncoder's public header is included by render_job_execute.cpp,
    # which belongs to chronon3d_cli_render. The CUDA interop type must be
    # visible on that compile target as well, not only on the video library.
    target_include_directories(chronon3d_cli_render PRIVATE
        "${CHRONON3D_CUDA_INCLUDE_DIR_CLI}")
    target_link_libraries(chronon3d_cli_video_export PRIVATE
        chronon3d_backend_vulkan "${CHRONON3D_CUDA_DRIVER_LIBRARY_CLI}"
        "${CHRONON3D_NVRTC_LIBRARY_CLI}")
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

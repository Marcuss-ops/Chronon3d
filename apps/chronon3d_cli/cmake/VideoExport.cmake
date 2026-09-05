if(NOT (CHRONON3D_ENABLE_VIDEO AND TARGET chronon3d_cli_render))
    return()
endif()

add_library(chronon3d_cli_video_export STATIC
    commands/video/common/pipe_export_helpers.cpp
    commands/video/exporters/pipe_export_pipeline_support.cpp
    commands/video/exporters/pipe_export_pipeline_setup.cpp
    commands/video/exporters/pipe_export_pipeline_loop.cpp
    commands/video/exporters/pipe_export_pipeline_warmup.cpp
    commands/video/exporters/pipe_timing_sidecar.cpp
    commands/video/exporters/pipe_timing_sidecar_frames.cpp
    commands/video/exporters/pipe_timing_sidecar_summary.cpp
    commands/video/exporters/pipe_timing_sidecar_diagnostics.cpp
    commands/video/exporters/pipe_export_finalize_encoder.cpp
    commands/video/exporters/pipe_export_finalize_telemetry.cpp
    commands/video/common/pipe_export_profile.cpp
    commands/video/common/pipe_export_stages.cpp
    commands/video/common/pipe_export_render_loop.cpp
    commands/video/common/pipe_export_direct_yuv.cpp
    commands/video/common/pipe_export_writer.cpp
    utils/video/video_sink_encoders.cpp
    utils/video/video_sink_adapter_config.cpp
    utils/video/video_sink_adapter_lifecycle.cpp
    utils/video/video_sink_adapter_submit.cpp
    utils/video/gop_smart_copy.cpp
)
if(NOT CHRONON3D_ENABLE_NATIVE_FFMPEG)
    target_sources(chronon3d_cli_video_export PRIVATE
        utils/video/video_runtime_stubs.cpp
        utils/video/hw_frame_ref_stubs.cpp)
endif()
set_target_properties(chronon3d_cli_video_export PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_cli_video_export PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli
)
target_link_libraries(chronon3d_cli_video_export PRIVATE
    chronon3d
    chronon3d_media_direct_yuv
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
        utils/video/native_av_encoder_common.cpp
        utils/video/native_av_encoder_open.cpp
        utils/video/native_av_encoder_lifecycle.cpp
        utils/video/native_av_encoder_cuda_queue.cpp
        utils/video/native_av_encoder_direct_yuv.cpp
        utils/video/native_av_encoder_native_surface.cpp
        utils/video/native_av_encoder_write.cpp
        utils/video/native_av_encoder_packets.cpp
    )
    target_link_libraries(chronon3d_cli_video_export PRIVATE
        chronon3d_ffmpeg_full chronon3d_media_native
    )
    target_compile_definitions(chronon3d_cli_video_export PRIVATE
        CHRONON3D_ENABLE_NATIVE_FFMPEG
    )
endif()

if(CHRONON3D_ENABLE_CUDA_INTEROP AND CHRONON3D_ENABLE_NATIVE_FFMPEG)
    set(CHRONON3D_NVRTC_ARCHITECTURE "compute_75" CACHE STRING
        "NVRTC virtual architecture for the CUDA video compositor (for example compute_86)")
    if(NOT CHRONON3D_NVRTC_ARCHITECTURE MATCHES "^compute_[0-9]+$")
        message(FATAL_ERROR
            "CHRONON3D_NVRTC_ARCHITECTURE must be compute_<capability>, got: "
            "${CHRONON3D_NVRTC_ARCHITECTURE}")
    endif()
    set(CHRONON3D_CUDA_INCLUDE_DIR_CLI "${CHRONON3D_CUDA_INCLUDE_DIR}")
    set(CHRONON3D_NVRTC_INCLUDE_DIR_CLI "${CHRONON3D_NVRTC_INCLUDE_DIR}")
    set(CHRONON3D_CUDA_DRIVER_LIBRARY_CLI "${CHRONON3D_CUDA_DRIVER_LIBRARY}")
    set(CHRONON3D_NVRTC_LIBRARY_CLI "${CHRONON3D_NVRTC_LIBRARY}")
    if(NOT CHRONON3D_CUDA_INCLUDE_DIR_CLI OR
       NOT CHRONON3D_CUDA_DRIVER_LIBRARY_CLI OR
       NOT CHRONON3D_NVRTC_INCLUDE_DIR_CLI OR
       NOT CHRONON3D_NVRTC_LIBRARY_CLI)
        message(FATAL_ERROR
            "CUDA interop was requested but CUDA driver/NVRTC headers or libraries "
            "were not found. Set CUDA_HOME/CUDAToolkit_ROOT or disable "
            "CHRONON3D_ENABLE_CUDA_INTEROP.")
    endif()
    target_include_directories(chronon3d_cli_video_export PRIVATE
        "${CHRONON3D_CUDA_INCLUDE_DIR_CLI}"
        "${CHRONON3D_NVRTC_INCLUDE_DIR_CLI}")
    target_include_directories(chronon3d_cli_render PRIVATE
        "${CHRONON3D_CUDA_INCLUDE_DIR_CLI}")
    target_include_directories(chronon3d_cli_core PRIVATE
        "${CHRONON3D_CUDA_INCLUDE_DIR_CLI}")
    target_link_libraries(chronon3d_cli_core PRIVATE
        "${CHRONON3D_CUDA_DRIVER_LIBRARY_CLI}")
    target_link_libraries(chronon3d_cli_video_export PRIVATE
        "${CHRONON3D_CUDA_DRIVER_LIBRARY_CLI}"
        "${CHRONON3D_NVRTC_LIBRARY_CLI}")
    if(TARGET chronon3d_backend_vulkan)
        target_link_libraries(chronon3d_cli_video_export PRIVATE
            chronon3d_backend_vulkan)
    endif()
    target_compile_definitions(chronon3d_cli_video_export PRIVATE
        CHRONON3D_NVRTC_ARCHITECTURE=\"${CHRONON3D_NVRTC_ARCHITECTURE}\")
    if(TARGET chronon3d_media_native)
        target_compile_definitions(chronon3d_media_native PRIVATE
            CHRONON3D_NVRTC_ARCHITECTURE=\"${CHRONON3D_NVRTC_ARCHITECTURE}\")
    endif()
endif()

target_sources(chronon3d_cli_render PRIVATE
    commands/video/common/video_export_common.cpp
    commands/video/exporters/video_export_pipe.cpp
    utils/video/video_job_validate.cpp
    utils/video/video_job_dry_run.cpp
    utils/video/video_job_execute_options.cpp
    utils/video/video_job_execute_dispatch.cpp
)
set_target_properties(chronon3d_cli_render PROPERTIES UNITY_BUILD OFF)
set_source_files_properties(
    commands/video/exporters/video_export_pipe.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)
target_link_libraries(chronon3d_cli_render PRIVATE
    chronon3d_cli_video_export
)
if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    target_link_libraries(chronon3d_cli_render PRIVATE
        chronon3d_ffmpeg_full chronon3d_media_native
    )
    target_compile_definitions(chronon3d_cli_render PRIVATE
        CHRONON3D_ENABLE_NATIVE_FFMPEG
    )
endif()
target_compile_definitions(chronon3d_cli_render PRIVATE
    CHRONON3D_HAS_CLI_VIDEO_EXPORT
)
target_compile_definitions(chronon3d_cli_core PRIVATE
    CHRONON3D_HAS_CLI_VIDEO_EXPORT
)

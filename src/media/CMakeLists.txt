# ==============================================================================
# src/media/CMakeLists.txt — chronon3d_media_interface + chronon3d_backend_video
#
# INTERFACE include dirs must use generator expressions so the installed
# Chronon3DTargets.cmake substitutes the install-time path (`include/`)
# instead of leaking the source-tree path.
# ==============================================================================

add_library(chronon3d_media_execution OBJECT
    video/video_execution_resolver.cpp
)
set_target_properties(chronon3d_media_execution PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_media_execution PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(chronon3d_media_execution PUBLIC chronon3d)

# Item 19: media interface target (abstract provider contract)
add_library(chronon3d_media_interface INTERFACE)
target_include_directories(chronon3d_media_interface INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(chronon3d_media_interface INTERFACE chronon3d chronon3d_media_execution)

if(CHRONON3D_ENABLE_VIDEO)
    # Native decode/mux mechanics belong to the media subsystem. The CLI
    # consumes these authorities and must not own a parallel libavformat path.
    if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
        add_library(chronon3d_media_native STATIC
            video/native_video_frame_decoder.cpp
            video/hw_frame_ref.cpp
            video/video_device_runtime.cpp
            video/mux/packet_assembler.cpp
        )
        target_include_directories(chronon3d_media_native PRIVATE
            ${CMAKE_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/include
        )
        target_link_libraries(chronon3d_media_native PUBLIC
            chronon3d_media_interface
            chronon3d_ffmpeg_full
        )
        set_target_properties(chronon3d_media_native PROPERTIES UNITY_BUILD OFF)
        if(CHRONON3D_ENABLE_CUDA_INTEROP)
            target_sources(chronon3d_media_native PRIVATE
                video/cuda_direct_nv12_compositor.cpp)
            target_include_directories(chronon3d_media_native PRIVATE
                "${CHRONON3D_CUDA_INCLUDE_DIR}"
                "${CHRONON3D_NVRTC_INCLUDE_DIR}")
            target_link_libraries(chronon3d_media_native PRIVATE
                "${CHRONON3D_CUDA_DRIVER_LIBRARY}"
                "${CHRONON3D_NVRTC_LIBRARY}")
            target_compile_definitions(chronon3d_media_native PRIVATE
                CHRONON3D_ENABLE_CUDA_INTEROP)
        endif()
    endif()

    # TICKET-011 cmake-boundary (OBJECT lift) — see chronon3d_backend_software comment.
    add_library(chronon3d_backend_video OBJECT
        frame_conversion/frame_converter.cpp
        frame_conversion/backends/packed_backend.cpp
        frame_conversion/backends/swscale_backend.cpp
        frame_conversion/converted_frame_cache.cpp
        frame_conversion/frame_conversion_service.cpp
        frame_conversion/encoder_frame_pool.cpp
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
        video/media_probe.cpp
        video/output_contract.cpp
        video/video_sink_factory.cpp
        video/video_config_validator.cpp
    )
    set_target_properties(chronon3d_media_video PROPERTIES UNITY_BUILD OFF)
    target_link_libraries(chronon3d_media_video PUBLIC
        chronon3d_media_interface
    )
    target_include_directories(chronon3d_media_video PRIVATE ${CMAKE_SOURCE_DIR})
    target_include_directories(chronon3d_media_video PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

    if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
        # The canonical compressed sink is in-process and shares the single
        # libavformat MuxSession authority with copied/NVENC packets.
        target_sources(chronon3d_media_video PRIVATE
            video/encode/native_av_sink.cpp)
        target_link_libraries(chronon3d_backend_video PUBLIC chronon3d_ffmpeg_light)
        target_link_libraries(chronon3d_media_video PUBLIC
            chronon3d_ffmpeg_full
            chronon3d_media_native)
        target_compile_definitions(chronon3d_media_video PRIVATE
            CHRONON3D_ENABLE_NATIVE_FFMPEG)
    endif()
    target_include_directories(chronon3d_backend_video PRIVATE ${CMAKE_SOURCE_DIR})
endif()

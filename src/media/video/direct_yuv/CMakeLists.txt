# DirectYUV is a media execution component, not a CLI implementation detail.
# The facade exists for every video build; native implementation details are
# compiled only when in-process FFmpeg is enabled.
add_library(chronon3d_media_direct_yuv STATIC)
set_target_properties(chronon3d_media_direct_yuv PROPERTIES UNITY_BUILD OFF)

target_include_directories(chronon3d_media_direct_yuv
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(chronon3d_media_direct_yuv PRIVATE
    chronon3d_pipeline
    spdlog::spdlog_header_only
)

if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    target_sources(chronon3d_media_direct_yuv PRIVATE
        direct_yuv_program.cpp
        direct_yuv_executor.cpp
        text_texture_cache.cpp
    )
    target_link_libraries(chronon3d_media_direct_yuv PUBLIC
        chronon3d_media_native
    )

    if(CHRONON3D_USE_BLEND2D)
        target_link_libraries(chronon3d_media_direct_yuv PRIVATE blend2d::blend2d)
        target_compile_definitions(chronon3d_media_direct_yuv PRIVATE CHRONON3D_USE_BLEND2D)
    endif()

    if(CHRONON3D_ENABLE_CUDA_INTEROP)
        target_compile_definitions(chronon3d_media_direct_yuv PRIVATE CHRONON3D_ENABLE_CUDA_INTEROP)
        if(CHRONON3D_CUDA_INCLUDE_DIR)
            target_include_directories(chronon3d_media_direct_yuv PRIVATE
                "${CHRONON3D_CUDA_INCLUDE_DIR}")
        endif()
        if(CHRONON3D_CUDA_DRIVER_LIBRARY)
            target_link_libraries(chronon3d_media_direct_yuv PRIVATE
                "${CHRONON3D_CUDA_DRIVER_LIBRARY}")
        endif()
    endif()
else()
    # The frontend still links the same media authority. Unsupported native
    # execution fails closed at prepare() without pulling libav/CUDA headers
    # into non-native builds.
    target_sources(chronon3d_media_direct_yuv PRIVATE
        direct_yuv_executor_stub.cpp
    )
endif()

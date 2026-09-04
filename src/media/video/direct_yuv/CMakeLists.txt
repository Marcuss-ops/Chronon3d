# DirectYUV is a media execution component, not a CLI implementation detail.
# It is intentionally a dedicated STATIC target so export frontends consume a
# stable media authority instead of compiling src/media sources themselves.
add_library(chronon3d_media_direct_yuv STATIC
    direct_yuv_program.cpp
    direct_yuv_executor.cpp
    text_texture_cache.cpp
)
set_target_properties(chronon3d_media_direct_yuv PROPERTIES UNITY_BUILD OFF)

target_include_directories(chronon3d_media_direct_yuv
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(chronon3d_media_direct_yuv
    PUBLIC
        chronon3d_media_native
    PRIVATE
        chronon3d_pipeline
        spdlog::spdlog_header_only
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

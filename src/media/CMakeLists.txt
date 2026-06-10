# ==============================================================================
# src/media/CMakeLists.txt — chronon3d_media_interface + chronon3d_backend_video
#
# Sottosistema video/media.  Definisce due target:
#
# chronon3d_media_interface (INTERFACE, header-only)
#   Contratto astratto MediaFrameProvider.  Senza dipendenze runtime —
#   solo include path e chronon3d base.
#   Fornisce a: chronon3d_graph (Item 19: graph dipende dall'interfaccia,
#               non dal backend video concreto)
#
# chronon3d_backend_video (STATIC)
#   Conversione frame, YUV diretto con Highway SIMD, cache frame convertiti.
#   Dipende da: chronon3d, chronon3d_media_interface, chronon3d_cache,
#               spdlog, fmt, TBB, hwy, FFmpeg (swscale + avutil)
#   Fornisce a: (aggregato chronon3d_core + chronon3d_software via WHOLE_ARCHIVE)
# ==============================================================================

# Item 19: media interface target (header-only — abstract provider contract)
add_library(chronon3d_media_interface INTERFACE)
target_include_directories(chronon3d_media_interface INTERFACE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(chronon3d_media_interface INTERFACE chronon3d)

# Backend video concreto — conversione formati, encoding YUV, cache
add_library(chronon3d_backend_video STATIC
    frame_conversion/frame_converter.cpp
    frame_conversion/direct_yuv_converter.cpp
    frame_conversion/direct_yuv_converter_hwy.cpp
    frame_conversion/converted_frame_cache.cpp
)
set_target_properties(chronon3d_backend_video PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_backend_video
    PUBLIC chronon3d chronon3d_media_interface chronon3d_cache
           spdlog::spdlog_header_only fmt::fmt TBB::tbb hwy::hwy
           ${CHRONON3D_FFMPEG_LIBS_LIGHT}
)
target_include_directories(chronon3d_backend_video PRIVATE ${CMAKE_SOURCE_DIR})

# ==============================================================================
# src/media/CMakeLists.txt — chronon3d_media_interface + chronon3d_backend_video
# ==============================================================================

# Item 19: media interface target (header-only — abstract provider contract)
add_library(chronon3d_media_interface INTERFACE)
target_include_directories(chronon3d_media_interface INTERFACE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(chronon3d_media_interface INTERFACE chronon3d)

add_library(chronon3d_backend_video STATIC
    frame_conversion/frame_converter.cpp
    frame_conversion/direct_yuv_converter.cpp
    frame_conversion/direct_yuv_converter_hwy.cpp
    frame_conversion/converted_frame_cache.cpp
)
set_target_properties(chronon3d_backend_video PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_backend_video PUBLIC
    chronon3d
    chronon3d_media_interface
    chronon3d_cache
    spdlog::spdlog_header_only
    fmt::fmt
    TBB::tbb
    hwy::hwy
    chronon3d_ffmpeg_light
)
target_include_directories(chronon3d_backend_video PRIVATE ${CMAKE_SOURCE_DIR})

# ==============================================================================
# src/backends/image/CMakeLists.txt — chronon3d_backend_image
# ==============================================================================

add_library(chronon3d_backend_image STATIC
    image_loader.cpp
    image_writer.cpp
    stb_image_backend.cpp)

if(CHRONON3D_ENABLE_EXR)
    target_sources(chronon3d_backend_image PRIVATE exr_mmap.cpp)
endif()

target_link_libraries(chronon3d_backend_image PUBLIC
    chronon3d
    spdlog::spdlog_header_only
    fmt::fmt
)

if(CHRONON3D_ENABLE_EXR)
    target_link_libraries(chronon3d_backend_image PUBLIC OpenEXR::OpenEXR)
endif()
target_include_directories(chronon3d_backend_image PRIVATE ${Stb_INCLUDE_DIR})
target_include_directories(chronon3d_backend_image PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_precompile_headers(chronon3d_backend_image PRIVATE <chronon3d/detail/pch.hpp>)

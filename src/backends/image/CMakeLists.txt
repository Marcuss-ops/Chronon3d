# ==============================================================================
# src/backends/image/CMakeLists.txt — chronon3d_backend_image (STATIC)
#
# Backend image I/O: caricamento (STB, OpenEXR), scrittura (PNG/PPM via STB),
# memory-mapped EXR per loading efficiente.
#
# Dipendenze (PUBLIC):
#   chronon3d              — tipi base (Framebuffer, Color)
#   spdlog (header-only)   — logging
#   fmt                    — formattazione
#   OpenEXR                — lettura/scrittura EXR (HDR)
#
# Include path:
#   ${Stb_INCLUDE_DIR}      — STB image loader headers
#
# Fornisce a: chronon3d_backend_assets, chronon3d_backend_software,
#             chronon3d_scene
# ==============================================================================
add_library(chronon3d_backend_image STATIC
    image_loader.cpp
    image_writer.cpp
    stb_image_backend.cpp
    exr_mmap.cpp
)
target_link_libraries(chronon3d_backend_image
    PUBLIC chronon3d spdlog::spdlog_header_only fmt::fmt OpenEXR::OpenEXR
)
target_include_directories(chronon3d_backend_image
    PRIVATE ${Stb_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/include
)
target_precompile_headers(chronon3d_backend_image PRIVATE <chronon3d/detail/pch.hpp>)

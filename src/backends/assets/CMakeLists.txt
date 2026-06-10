# ==============================================================================
# src/backends/assets/CMakeLists.txt — chronon3d_backend_assets (STATIC)
#
# Backend asset management: cache immagini caricate, renderer immagini
# (compositing di immagini sul framebuffer).
#
# Dipendenze (PUBLIC):
#   chronon3d                — tipi base
#   chronon3d_backend_image  — image I/O (STB, EXR)
#
# Include path:
#   ${Stb_INCLUDE_DIR}       — STB image loader headers
#
# Fornisce a: chronon3d_backend_software (via asset rendering pipeline)
# ==============================================================================
add_library(chronon3d_backend_assets STATIC
    image_cache.cpp
    image_renderer.cpp
)
target_link_libraries(chronon3d_backend_assets
    PUBLIC chronon3d chronon3d_backend_image
)
target_include_directories(chronon3d_backend_assets PRIVATE ${Stb_INCLUDE_DIR})

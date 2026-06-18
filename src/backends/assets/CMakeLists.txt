# ==============================================================================
# src/backends/assets/CMakeLists.txt — chronon3d_backend_assets
# ==============================================================================

add_library(chronon3d_backend_assets STATIC
    image_cache.cpp
    image_renderer.cpp
)
target_link_libraries(chronon3d_backend_assets PUBLIC chronon3d chronon3d_backend_image chronon3d_backend_software)
target_include_directories(chronon3d_backend_assets PRIVATE ${Stb_INCLUDE_DIR})

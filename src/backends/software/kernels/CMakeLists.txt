# ==============================================================================
# src/backends/software/kernels/CMakeLists.txt
# ==============================================================================

target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/grid_background_kernel.cpp
)

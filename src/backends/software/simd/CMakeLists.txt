# ==============================================================================
# src/backends/software/simd/CMakeLists.txt
# ==============================================================================

target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/highway_color_kernels.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/highway_blend_kernels.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/highway_separable_kernels.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/highway_conversion_kernels.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/highway_rasterize_kernels.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/highway_dof_kernels.cpp
)

# ==============================================================================
# src/backends/software/processors/effects/generate/CMakeLists.txt
# ==============================================================================
# Generate effect processors.

target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fill_noise_offset.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fractal_noise.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/checkerboard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/four_color_gradient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/light_rays.cpp
)

# ==============================================================================
# src/backends/software/processors/effects/blur/CMakeLists.txt
# ==============================================================================
# Blur effect processors — populated by A5 (DirectionalBlur) and A6 (RadialBlur).

target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/directional_blur.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/radial_blur.cpp
)

# ==============================================================================
# src/backends/software/processors/effects/generate/CMakeLists.txt
# ==============================================================================
# Generate effect processors — populated by A4 (Fill, Noise, Offset).

target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/fill_noise_offset.cpp
)

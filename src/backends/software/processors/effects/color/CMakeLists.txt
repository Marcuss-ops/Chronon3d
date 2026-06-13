# ==============================================================================
# src/backends/software/processors/effects/color/CMakeLists.txt
# ==============================================================================
# Color effect processors — populated by A2 (Exposure, Levels) and A3 (Curves).

target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/exposure_levels.cpp
)

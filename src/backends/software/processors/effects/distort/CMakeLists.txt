# =============================================================================
# src/backends/software/processors/effects/distort/CMakeLists.txt
# =============================================================================
# Warp family (CornerPin, Twirl, Bulge, MeshWarp) effect processors.

target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/warp.cpp
)

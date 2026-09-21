# ==============================================================================
# src/backends/software/utils/CMakeLists.txt
# Backend-internal analysis and projection helpers.  The Blend2D bridge
# (blend2d_bridge_*, blend2d_paint_*) was consolidated into
# src/backends/software/blend2d/ on 2026-09-20 — this directory no longer
# owns a Blend2D entry point.
# ==============================================================================

# Backend-internal analysis, projection and effect helpers.
target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/framebuffer_analysis.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/projection_utils.cpp
)

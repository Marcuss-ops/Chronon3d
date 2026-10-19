# =============================================================================
# src/backends/software/processors/effects/postprocess/CMakeLists.txt
# =============================================================================
# Post-process FX family (Scanlines, RetroTv, Halation, AnamorphicStreak)
# software kernels.

target_sources(chronon3d_backend_software PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/postprocess_fx.cpp
)

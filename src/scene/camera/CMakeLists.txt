# ==============================================================================
# src/scene/camera/CMakeLists.txt
# ==============================================================================

target_sources(chronon3d_scene PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_projection_source.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_projection.cpp
)

# Renderer camera consumed surface only: projection + optics lowering.
# Camera authoring and evaluation live in ChrononMotion3d. The unconsumed camera
# debug overlay / shot report / evaluated projection snapshot were demolished;
# CameraStateBuilder remains a small value helper used by scene/test clients.
# Dev diagnostics live in backends/software/diagnostics.

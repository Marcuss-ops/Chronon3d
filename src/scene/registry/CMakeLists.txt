# ==============================================================================
# src/scene/registry/CMakeLists.txt
#
# Camera System V1 registries (P2/P5):
#   - CameraMotionRegistry       — preset registration + lookup
#   - CameraConstraintRegistry   — constraint factory registration
#
# Aggregated into chronon3d_scene. No new public deps required.
# ==============================================================================

target_sources(chronon3d_scene PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_motion_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_constraint_registry.cpp
)

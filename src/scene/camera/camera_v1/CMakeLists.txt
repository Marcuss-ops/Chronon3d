# ==============================================================================
# src/scene/camera/camera_v1/CMakeLists.txt
#
# Camera System V1 sources (P2–P5):
#   - CameraMotionRegistry, CameraConstraintRegistry (P2)
#   - CameraTrajectory (P3)
#   - CameraConstraint + Stack + Resolver (P5)
#   - CameraProgram (P4)
#
# These are added directly to chronon3d_scene via target_sources. They use
# only public types from chronon3d::core so no new private deps are needed.
# ==============================================================================

target_sources(chronon3d_scene PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_trajectory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_program.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/register_camera_v1.cpp
)

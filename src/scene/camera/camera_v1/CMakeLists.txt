# ==============================================================================
# src/scene/camera/camera_v1/CMakeLists.txt
#
# Camera System V1 sources:
#   - CameraTrajectory
#   - CameraConstraint + factory functions
#   - CameraProgram
#   - CameraProgramCompiler (compiled path)
#
# These are added directly to chronon3d_scene via target_sources. They use
# only public types from chronon3d::core so no new private deps are needed.
# ==============================================================================

target_sources(chronon3d_scene PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_trajectory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_program.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/register_camera_v1.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_framing_solver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shot_timeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_motion_blur.cpp
    # PR1 — CameraDescriptor → compiled CameraProgram compiler.
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_program_compiler.cpp
)

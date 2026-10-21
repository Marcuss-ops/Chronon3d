# ==============================================================================
# src/scene/camera/camera_v1/CMakeLists.txt
#
# Camera System V1 sources:
#   - CameraTrajectory
#   - CameraConstraint + factory functions
#   - CameraProgram
#   - CameraProgramCompiler (compiled path)
#
# This target is linked only into legacy camera tests during the CM3D migration.
# The renderer's chronon3d_scene target must not contain camera authoring code.
# ==============================================================================

add_library(chronon3d_camera_v1_legacy OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_trajectory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_program.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_program_orientation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_program_source_dispatch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_program_evaluate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_program_sources.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_program_constraints.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/register_camera_v1.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_framing_solver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shot_timeline.cpp
    # PR1 — CameraDescriptor → compiled CameraProgram compiler.
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_program_compiler.cpp
    # TICKET-031 — canonical stateful-constraint pre-roll + fingerprint
    # invalidation + Cut reset + per-worker session isolation.
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_session_cache.cpp
)
target_include_directories(chronon3d_camera_v1_legacy PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(chronon3d_camera_v1_legacy PRIVATE chronon3d_scene)

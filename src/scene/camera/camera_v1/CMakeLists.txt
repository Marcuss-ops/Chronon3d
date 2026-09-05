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
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_program_orientation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_program_source_dispatch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_program_evaluate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_program_sources.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_program_constraints.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/register_camera_v1.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_framing_solver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shot_timeline.cpp
    # Canonical internal shutter-pose sampler implementation.
    # that re-exports `ShutterPoseSampler` for ABI compatibility.
    ${CMAKE_CURRENT_SOURCE_DIR}/internal/shutter_pose_sampler.cpp
    # PR1 — CameraDescriptor → compiled CameraProgram compiler.
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_program_compiler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_builtin_presets.cpp
    # CameraMotionParamsSource evaluation and its authoring adapter.
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_descriptor_adapters.cpp
    # TICKET-031 — canonical stateful-constraint pre-roll + fingerprint
    # invalidation + Cut reset + per-worker session isolation.
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_session_cache.cpp
)

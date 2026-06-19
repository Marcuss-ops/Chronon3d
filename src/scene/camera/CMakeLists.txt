# ==============================================================================
# src/scene/camera/CMakeLists.txt
# ==============================================================================

target_sources(chronon3d_scene PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_projection_source.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_motion_presets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_rig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_rig_presets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_projection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_shot_validator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_framing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_path_sampler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/camera_debug_overlay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay_hud_panels.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay_kinematic_panels.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay_spatial_panels.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay_diagnostic_panels.cpp
)

# Camera System V1 subdirectory: trajectory, program, constraints, compiler.
# See src/scene/camera/camera_v1/CMakeLists.txt for files.
add_subdirectory(camera_v1)

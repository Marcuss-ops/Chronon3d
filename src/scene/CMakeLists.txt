# ==============================================================================
# src/scene/CMakeLists.txt — chronon3d_scene
#
# This target aggregates sources from scene/, timeline/, animations/camera/,
# layout/, math/, presets/, specscene/, media/, and vector/. Sources outside
# scene/ are referenced via relative paths (e.g. ../timeline/...).
# ==============================================================================

add_library(chronon3d_scene STATIC
    # Local files outside subsystem dirs
    utils/dark_grid_background.cpp
    # Moved into src/scene/ to eliminate ../ cross-directory references
    timeline_builder.cpp
    camera_tilt_clips.cpp
    camera_motion_applier.cpp
    layout_solver.cpp
    path_utils.cpp
    media_placement.cpp
    path_factories.cpp
    presets/camera_motion_clip.cpp
    presets/motion_renderer.cpp
    presets/motion_resolver.cpp
    presets/motion_preset_registry.cpp
    presets/motion_presets_reveal.cpp
    presets/motion_presets_3d.cpp
    presets/motion_presets_glow.cpp
    presets/motion_presets_idle.cpp
    presets/motion_presets_impact.cpp
    presets/phrase_presets.cpp
    presets/phrase_group_builder.cpp
    specscene/specscene.cpp
    specscene/specscene_parsers.cpp
    specscene/specscene_compiler.cpp
    specscene/specscene_validator.cpp
)

# ==============================================================================
# Subsystem source files (each directory adds its sources via target_sources)
# ==============================================================================
add_subdirectory(camera)
add_subdirectory(model)
add_subdirectory(builders)
add_subdirectory(validation)
target_include_directories(chronon3d_scene PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_scene
    PUBLIC
        chronon3d
        chronon3d_runtime
        chronon3d_graph
        chronon3d_registry
        chronon3d_cache
        chronon3d_effects
        chronon3d_backend_image
    PRIVATE
        spdlog::spdlog_header_only
        fmt::fmt
        tomlplusplus::tomlplusplus
)
target_include_directories(chronon3d_scene PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_precompile_headers(chronon3d_scene PRIVATE <chronon3d/detail/pch.hpp>)
